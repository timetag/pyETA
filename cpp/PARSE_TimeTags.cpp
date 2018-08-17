/************************************************************************
Marcus Sackrow, PicoQuant_header_parser GmbH, December 2013
Michael Wahl, PicoQuant_header_parser GmbH, revised July 2014
Zuzeng Lin, KTH, 2017-2018
************************************************************************/

#ifdef __clang__
#define MKS_inline __attribute__((always_inline))
#else

#define MKS_inline 
#endif // _MSC_VER
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef unsigned long  DWORD;
#define PERROR(...) {order_gurantee3=printf( "\n [ERROR]"  __VA_ARGS__ );}
#define PINFO(...)  {order_gurantee3=printf("\n" __VA_ARGS__);}

extern "C" {

	long long order_gurantee3 = 0;

	typedef struct {
		long long fseekpoint;//0
		long long fendpoint ;//1
		long long TTRes_pspr ;//2
		long long DTRes_pspr ;//3
		long long SYNCRate_pspr ;//4

		long long BytesofRecords ;//5
		long long RecordType ;//6
			long long batch_nextreadpos_in_file ;//7
			long long batch_actualread_length ;//8
			long long next_RecID_in_batch ;//9

			long long overflowcorrection ;//10
			long long resuming ;//11
			char *buffer = 0;//12
			FILE *fpttf;//13
	}ttf_reader;

	//DANGER: globlal
	static ttf_reader *READERs;
	//DANGER: globlal


#define batchreadRecNum  10000
	// RecordTypes
#define rtPicoHarpT3     0x00010303    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $03 (PicoHarp)
#define rtPicoHarpT2     0x00010203    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $03 (PicoHarp)
#define rtHydraHarpT3    0x00010304    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarpT2    0x00010204    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtHydraHarp2T3   0x01010304    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarp2T2   0x01010204    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtTimeHarp260NT3 0x00010305    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $05 (TimeHarp260N)
#define rtTimeHarp260NT2 0x00010205    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $05 (TimeHarp260N)
#define rtTimeHarp260PT3 0x00010306    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $06 (TimeHarp260P)
#define rtTimeHarp260PT2 0x00010206    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $06 (TimeHarp260P)
#define quTAU 0 
#define SwebianInstrument 1 
#define MARKER_OFFSET 16

//Got GotRelativeSignal
//  DTimeTag: Arrival time of Photon after last Sync event (T3 only) 
// DTimeTag * DTRes_pspr = Real time arrival of Photon after last Sync event
//  virtual_channel: virtual_channel the Photon arrived (0 = Sync channel for T2 measurements)
#define GotRelativeSignal( SYNC_counts,  Channel,  DTimeTag) \
	{ oAbsTime_ps= (SYNC_counts) *READERs[0].SYNCRate_pspr + (DTimeTag)*READERs[0].DTRes_pspr; \
	oChannel = (Channel);\
	 }


	//GotAbsoluteSignal
	//  TimeTag: Raw TimeTag from Record * TTRes_pspr = Real Time arrival of Photon
	#define GotAbsoluteSignal(TimeTag , Channel) { oAbsTime_ps= TimeTag *READERs[0].TTRes_pspr; oChannel= Channel;}

	void MKS_inline ProcessPHT2(unsigned int TTTRRecord, long long &oAbsTime_ps, unsigned char &oChannel, long long &oflcorrection)
	{

		const int T2WRAPAROUND = 210698240;
		long long truetime;
		union
		{
			unsigned int allbits;
			struct
			{
				unsigned time : 28;
				unsigned channel : 4;
			} bits;

		} Record;
		unsigned int markers;
		Record.allbits = TTTRRecord;
		if (Record.bits.channel == 0xF) //this means we have a special record
		{
			//in a special record the lower 4 bits of time are marker bits
			markers = Record.bits.time & 0xF;
			if (markers == 0) //this means we have an overflow record
			{
				oflcorrection += T2WRAPAROUND; // unwrap the time tag overflow
											   //GotOverflow( 1);
			}
			else //a marker
			{
				//Strictly, in case of a marker, the lower 4 bits of time are invalid
				//because they carry the marker bits. So one could zero them out.
				//However, the marker resolution is only a few tens of nanoseconds anyway,
				//so we can just ignore the few picoseconds of error.
				truetime = oflcorrection + Record.bits.time;
				GotAbsoluteSignal(truetime, MARKER_OFFSET+markers);
				//GotMarker( truetime, markers);
			}
		}
		else
		{
			if ((int)Record.bits.channel > 4) //Should not occur
			{
				PERROR(" Illegal Chan:  %1u\n", Record.bits.channel);
			}
			else
			{

				truetime = oflcorrection + Record.bits.time;
				GotAbsoluteSignal(truetime, Record.bits.channel);
			}
		}
	}

	void MKS_inline ProcessHHT2(unsigned int TTTRRecord, int HHVersion, long long &oAbsTime_ps, unsigned char &oChannel, long long &oflcorrection)
	{

		long long truetime;
		const int T2WRAPAROUND_V1 = 33552000;
		const int T2WRAPAROUND_V2 = 33554432;
		union {
			DWORD   allbits;
			struct {
				unsigned timetag : 25;
				unsigned channel : 6;
				unsigned special : 1; // or sync, if channel==0
			} bits;
		} T2Rec;
		T2Rec.allbits = TTTRRecord;

		if (T2Rec.bits.special == 1)
		{
			if (T2Rec.bits.channel == 0x3F) //an overflow record
			{
				if (HHVersion == 1)
				{
					oflcorrection += (unsigned __int64)T2WRAPAROUND_V1;
					//GotOverflow( 1);
				}
				else
				{
					//number of overflows is stored in timetag
					if (T2Rec.bits.timetag == 0) //if it is zero it is an old style single overflow
					{
						//GotOverflow( 1);
						oflcorrection += (unsigned __int64)T2WRAPAROUND_V2;  //should never happen with new Firmware!
					}
					else
					{
						oflcorrection += (unsigned __int64)T2WRAPAROUND_V2 * T2Rec.bits.timetag;
						//GotOverflow( T2Rec.bits.timetag);
					}
				}
			}

			if ((T2Rec.bits.channel >= 1) && (T2Rec.bits.channel <= 15)) //markers
			{
				truetime = oflcorrection + T2Rec.bits.timetag;
				//Note that actual marker tagging accuracy is only some ns.
				GotAbsoluteSignal(truetime, MARKER_OFFSET+ T2Rec.bits.channel)
				//GotMarker(truetime, T2Rec.bits.channel);
			}

			if (T2Rec.bits.channel == 0) //sync
			{
				truetime = oflcorrection + T2Rec.bits.timetag;
				GotAbsoluteSignal(truetime, 0);
			}
		}
		else //regular input channel
		{
			truetime = oflcorrection + T2Rec.bits.timetag;

			GotAbsoluteSignal(truetime, T2Rec.bits.channel + 1);
		}

	}


	// PicoHarp T3 input
	void MKS_inline ProcessPHT3(unsigned int TTTRRecord, long long &oAbsTime_ps, unsigned char &oChannel, long long &oflcorrection)
	{
		long long truensync;
		const int T3WRAPAROUND = 65536;
		union
		{
			unsigned int allbits;
			struct
			{
				unsigned numsync : 16;
				unsigned dtime : 12;
				unsigned channel : 4;
			} bits;
			struct
			{
				unsigned numsync : 16;
				unsigned markers : 12;
				unsigned channel : 4;
			} special;
		} Record;

		Record.allbits = TTTRRecord;
		if (Record.bits.channel == 0xF) //this means we have a special record
		{
			if (Record.special.markers == 0) //not a marker means overflow
			{
				//GotOverflow(1);
				oflcorrection += T3WRAPAROUND; // unwrap the time tag overflow
			}
			else
			{
				truensync = oflcorrection + Record.bits.numsync;
				//GotMarker( truensync, Record.special.markers);
				GotRelativeSignal(truensync, MARKER_OFFSET+Record.special.markers, 0);
			}
		}
		else
		{
			if (
				(Record.bits.channel == 0) //Should never occur in T3 Mode
				|| (Record.bits.channel>4) //Should not occur with current routers
				)
			{
				PERROR("\nIllegal virtual_channel:  %1u", Record.bits.channel);
			}

			truensync = oflcorrection + Record.bits.numsync;

			GotRelativeSignal(truensync, Record.bits.channel, Record.bits.dtime);

		}
	};
	void MKS_inline ProcessHHT3(unsigned int TTTRRecord, int HHVersion, long long &oAbsTime_ps, unsigned char &oChannel, long long &oflcorrection)
	{

		const auto T3WRAPAROUND = 1024;
		union {
			unsigned int allbits;
			struct {
				unsigned nsync : 10;  // numer of sync period
				unsigned dtime : 15;    // delay from last sync in units of chosen resolution
				unsigned channel : 6;
				unsigned special : 1;
			} bits;
		} T3Rec;
		T3Rec.allbits = TTTRRecord;
		if (T3Rec.bits.special == 1)
		{
			if (T3Rec.bits.channel == 0x3F) //overflow
			{
				//number of overflows is stored in nsync
				if ((T3Rec.bits.nsync == 0) || (HHVersion == 1)) //if it is zero or old version it is an old style single overflow
				{
					oflcorrection += (unsigned __int64)T3WRAPAROUND;
					//GotOverflow(1); 
					//should never happen with new Firmware!
				}
				else
				{
					oflcorrection += (unsigned __int64)T3WRAPAROUND * T3Rec.bits.nsync;
					//GotOverflow( T3Rec.bits.nsync);
				}
				
			}
			if ((T3Rec.bits.channel >= 1) && (T3Rec.bits.channel <= 15)) //markers
			{
				
				//the time unit depends on sync period which can be obtained from the file header
				GotRelativeSignal(oflcorrection + T3Rec.bits.nsync, MARKER_OFFSET + T3Rec.bits.channel, 0);
				//GotMarker(truensync, T3Rec.bits.channel);

			}
		}
		else //regular input channel
		{
			
			//the nsync time unit depends on sync period which can be obtained from the file header
			//the dtime unit depends on the resolution and can also be obtained from the file header
			GotRelativeSignal(oflcorrection + T3Rec.bits.nsync, T3Rec.bits.channel, T3Rec.bits.dtime);

		}
	}



	/////////////////////////////////////////////////////////////////////////////////
	//external
	/////////////////////////////////////////////////////////////////////////////////

	int MKS_inline read_next_minibatch() {
		READERs[0].batch_actualread_length = fread(READERs[0].buffer, READERs[0].BytesofRecords, batchreadRecNum, READERs[0].fpttf)*READERs[0].BytesofRecords;
		READERs[0].batch_nextreadpos_in_file += READERs[0].batch_actualread_length;
		READERs[0].next_RecID_in_batch = 0; 
		return READERs[0].batch_actualread_length;
	}
	long long  MKS_inline pop_signal_from_file(unsigned char *out_Channel) {
		
		//PINFO("overflowcorrection %lld \n ", READERs[0].overflowcorrection)
		while (true) {
			long long AbsTime_ps = INT64_MAX;
			unsigned char Channel = 255;
			const auto next_relpos = READERs[0].next_RecID_in_batch*READERs[0].BytesofRecords;
			const auto batch_head_abspos = READERs[0].batch_nextreadpos_in_file - READERs[0].batch_actualread_length;
			const auto next_abspos = batch_head_abspos + next_relpos;
			
			if (next_relpos >= READERs[0].batch_actualread_length)
			{
				//read next batch
				if (read_next_minibatch() <= 0) {
					PINFO("Reader %x for section [%lld %lld) is reaching end, nextrec %lld, batchend %lld, file is not long enough.", READERs, READERs[0].fseekpoint, READERs[0].fendpoint, next_abspos, READERs[0].batch_nextreadpos_in_file);
					break;
				}
			}
			
				
			//boundry check
			if (next_abspos >= READERs[0].fendpoint)
			{
				PINFO("Reader %x for section [%lld %lld) is reaching end, nextrec %lld, batchend %lld, boundry.", READERs, READERs[0].fseekpoint, READERs[0].fendpoint,next_abspos,READERs[0].batch_nextreadpos_in_file);
				break;
			}


			//parse binary
			const auto TTTRRecord = ((unsigned int*)READERs[0].buffer)[READERs[0].next_RecID_in_batch];
			switch (READERs[0].RecordType)
			{

					//picoharp_parsers
					case rtPicoHarpT2:
						ProcessPHT2(TTTRRecord, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;
					case rtPicoHarpT3:
						ProcessPHT3(TTTRRecord, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;
					case rtHydraHarpT2:
						ProcessHHT2(TTTRRecord, 1, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;
					case rtHydraHarpT3:
						ProcessHHT3(TTTRRecord, 1, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;
					case rtHydraHarp2T2:
					case rtTimeHarp260NT2:
					case rtTimeHarp260PT2:
						ProcessHHT2(TTTRRecord, 2, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;

					case rtHydraHarp2T3:
					case rtTimeHarp260NT3:
					case rtTimeHarp260PT3:

						ProcessHHT3(TTTRRecord, 2, AbsTime_ps, Channel, READERs[0].overflowcorrection);
						break;
					case quTAU: {
	
						union TTTRRecord
						{
							unsigned char allbits[16];
							struct {
								uint64_t time;
								uint16_t channel;
								char blank[6];
							} bits;
						};
						TTTRRecord *TTTRRecordPtr;
						TTTRRecordPtr = (TTTRRecord *)(READERs[0].buffer + READERs[0].next_RecID_in_batch * READERs[0].BytesofRecords);
						AbsTime_ps = (*TTTRRecordPtr).bits.time *READERs[0].TTRes_pspr;
						Channel = (*TTTRRecordPtr).bits.channel;
						break;
					}
					case SwebianInstrument: {
						struct SITTTRStruct
						{
							int overflow;
							int channel;
							unsigned long long time;
						};
						SITTTRStruct *TTTRRecordPtr;
						TTTRRecordPtr = (SITTTRStruct *)(READERs[0].buffer + READERs[0].next_RecID_in_batch * READERs[0].BytesofRecords);
						AbsTime_ps = (*TTTRRecordPtr).time *READERs[0].TTRes_pspr;
						Channel = (*TTTRRecordPtr).channel;
						break;
					}
					default:
					{
						PERROR("ERROR: Unsupported timetag format.")
						break;
					}
			}
			READERs[0].next_RecID_in_batch++;


			//overflow, try again next time
			if (AbsTime_ps == INT64_MAX) {
				continue;
			}
			else {
				*out_Channel = Channel;
				return AbsTime_ps;
			}	
		}
		*out_Channel = 255;
		return INT64_MAX;
	}
	int MKS_inline FileReader_init(char* filename, void* ptr) {
		READERs = (ttf_reader*)ptr;


		// reset nextreadpos to seekpoint

		READERs[0].batch_nextreadpos_in_file = READERs[0].fseekpoint;
		READERs[0].next_RecID_in_batch = 0;
		READERs[0].batch_actualread_length = 0;
		//open file
		if ((READERs[0].fpttf = fopen(filename, "rb")) == NULL)
		{
			PERROR("Time-tag file cannot be opened, aborting.\n");
			return -1;
		}
		// seek to starting point
		if ((_fseeki64(READERs[0].fpttf, READERs[0].batch_nextreadpos_in_file, SEEK_SET)) != NULL) {
			PERROR("Time-tag file cannot seek, aborting.\n");
			return -1;
		}
		//create buffer
		if ((READERs[0].buffer = (char *)malloc(batchreadRecNum *READERs[0].BytesofRecords)) == NULL) {
			PERROR("Reading buffer for Time-tag file is not assgined properly, aborting.\n");
			return -1;
		}
		PINFO("\nReader %x is assigned to a section of [%lld,%lld)\n", READERs,READERs[0].fseekpoint, READERs[0].fendpoint);

		/*PINFO("TTRes_pspr %lld", READERs[0].TTRes_pspr);
		PINFO("DTRes_pspr %lld", READERs[0].DTRes_pspr);
		PINFO("SYNCRate_pspr %lld", READERs[0].SYNCRate_pspr);
		PINFO("BytesofRecords %lld", READERs[0].BytesofRecords);
		PINFO("RecordType %lld", READERs[0].RecordType);*/
		return 0;
	}
	int MKS_inline FileReader_close( void* ptr) {
		READERs = (ttf_reader*)ptr;
		if (fclose(READERs[0].fpttf) != NULL)
		{
			PERROR("Time-tag file cannot be closed, aborting.\n");
			return -1;
		}
		free(READERs[0].buffer);
		PINFO("\nReader is closing.");
	
		return 0;
	}
}