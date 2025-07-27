#include "TeensyControls.h"
#include "fs2020.h"

const int xplmType_Int = 1;
const int xplmType_Float = 2;
const int xplmType_Double = 3;
const int xplmType_IntArray = 4;
const int xplmType_FloatArray = 5;
const int xplmType_Data = 6;

static void input_packet(teensy_t *t, const uint8_t *packet);
static int  output_data(teensy_t *t, const uint8_t *data, int datalen);
static void output_flush(teensy_t *t);


// process all buffered input
// elapsed is time in seconds since previous input
// flags = 1 upon enable event
// flags = 2 upon disable event
//
void TeensyControls_input(float elapsedNotUsed, int flags)
{
	teensy_t *t;
	uint8_t packet[64];

	for (t = TeensyControls_first_teensy; t; t = t->next) {
		while (TeensyControls_input_fetch(t, packet)) {
			input_packet(t, packet);
		}
	}
}

static float bytes2float(const void *ptr)
{
	union {
		uint8_t b[4];
		float f;
	} u;
	memcpy(u.b, ptr, 4);
	return u.f;
}

static int32_t bytes2in32(const void *ptr)
{
	union {
		uint8_t b[4];
		int32_t i;
	} u;
	memcpy(u.b, ptr, 4);
	return u.i;
}

static void decode_packet(teensy_t *t, const uint8_t *packetPtr, uint8_t len)
{
	int cmd, id, type;
	item_t *item;
	int32_t intval;
	float floatval;
	int floatTrunc;
	char *name;

	cmd = *(packetPtr+1);
	switch (cmd) {
	  case 0x01: // register command or data
		if (len < 7) break;
		id = *(packetPtr + 2) | (*(packetPtr + 3) << 8);
		type = *(packetPtr + 4);
		name = (char *)(packetPtr + 6);
		TeensyControls_new_item(t, id, type, name, len - 6);
		break;

	  case 0x02: // write data data
		if (len < 10) break;
		id = *(packetPtr + 2) | (*(packetPtr + 3) << 8);
		type = *(packetPtr + 4);
		item = TeensyControls_find_item(t, id);
		if (!item) {
			printf("Cannot write data due to unmapped Data Ref #%d\n", id);
			// Request all ids from Teensy again
			//t->unknown_id_heard = 1;
			break;
		}
		//printf("WriteData id: %d  type: %d  item: %s\n", id, type, item->name);
		//if (item->type != type || item->datawritable == 0) break;
		intval = *(packetPtr + 6) | (*(packetPtr + 7) << 8)
			| (*(packetPtr + 8) << 16) | (*(packetPtr + 9) << 24);
		if (type == 1) { // integer
			item->intval = intval;
			item->intval_remote = intval;
			item->changed_by_teensy = 1;
		} else if (type == 2) { // float
			floatval = bytes2float(&intval);
			floatTrunc = floatval * 1000.0;
			floatval = floatTrunc / 1000.0;
			item->floatval = floatval;
			item->floatval_remote = floatval;
			item->changed_by_teensy = 1;
		}
		break;

	  case 0x04: // command begin
		if (len < 4) break;
		id = *(packetPtr + 2) | (*(packetPtr + 3) << 8);
		item = TeensyControls_find_item(t, id);
		if (!item) {
			t->unknown_id_heard = 1;
			printf("CommandBegin id: %d  Unknown item\n", id);
			break;
		}
		printf("CommandBegin id: %d  type: %d  name: %s\n", id, item->type, item->name);
		if (item->type != 0 || item->command_count >= 128) break;
		item->command_queue[item->command_count++] = cmd;
		printf("Command Begin: id=%d, name=%s\n", id, item->name);
		break;

	  case 0x05: // command end
		if (len < 4) break;
		id = *(packetPtr + 2) | (*(packetPtr + 3) << 8);
		item = TeensyControls_find_item(t, id);
		if (!item) {
			t->unknown_id_heard = 1;
			printf("CommandEnd id: %d  Unknown item\n", id);
			break;
		}
		printf("CommandEnd id: %d  type: %d  name: %s\n", id, item->type, item->name);
		if (item->type != 0 || item->command_count >= 128) break;
		item->command_queue[item->command_count++] = cmd;
		printf("Command End: id=%d, name=%s\n", id, item->name);
		break;

	  case 0x06: // command once
		if (len < 4) break;
		id = *(packetPtr + 2) | (*(packetPtr + 3) << 8);
		item = TeensyControls_find_item(t, id);
		if (!item) {
			t->unknown_id_heard = 1;
			printf("CommandOnce id: %d  Unknown item\n", id);
			break;
		}
		printf("CommandOnce id: %d  type: %d  name: %s\n", id, item->type, item->name);
		if (item->type != 0 || item->command_count >= 128) break;
		item->command_queue[item->command_count++] = cmd;
		printf("Command Once: id=%d, name=%s\n", id, item->name);
		break;
	}
}

static void input_packet(teensy_t *t, const uint8_t *packet) {
	uint8_t i, j, cmd, fragment_id;
	uint16_t len;
	char ch;
    
#if 0
	printf("Input:\n");
	for (i=0; i<4; i++) {

		printf("%02x  ", i*8);
		for (j=0; j<16; j++) {
			printf(" %02x", packet[i*16+j]);
		}

		printf("  ");
		for (j=0; j<16; j++) {
			ch = (char)packet[i*16+j];
			if (ch<0x20 || ch>0x7e) {
				printf(".");
			} else {
				printf("%c",ch);
			}
		}
		printf("\n");
	}
#endif
	i = 0;
	do {
		len = packet[i];
		//printf("len=%d\n",len);
		if (len < 2 ) return;
		if (len > 64-i) {
			if (packet[i+1] == 0xff) {
				printf("Long Teensy command fragment with len>buffer space, not allowed (len=%d, bufspace=%d, cmd=%02x)\n", len, 64-i, packet[i+1]);
				return;
			}
			t->input_packet_bytes_missing = (len-(64-i));
			t->input_packet_ptr = t->input_packet;
			t->expect_fragment_id = 1;
			memcpy(t->input_packet_ptr,&packet[i],64-i);
			t->input_packet_ptr += (64-i);
			//printf("Start of long Teensy command received, %d bytes missing\n", t->input_packet_bytes_missing);
			return;  // leave here, packet complete
		}
		cmd = packet[i + 1];
		//printf("cmd=%d\n",cmd);

		if (cmd != 0xFF) {
			if (t->expect_fragment_id != 0) {
				printf("Expected Teensy command fragment %d not received (cmd=%d)\n", t->expect_fragment_id, cmd);
				t->expect_fragment_id=0;
			}

			decode_packet(t,&packet[i],len);
		} else {
			fragment_id = packet[i+2];
			if (fragment_id != t->expect_fragment_id) {
				  printf("Unexpected Teensy command fragment %d received, expected: %d\n", fragment_id, t->expect_fragment_id);
				  t->expect_fragment_id=0;
				  return;
			}
			//printf("Teensy command fragment %d received, len=%d, ptr=%d\n", fragment_id, len, (int)(t->input_packet_ptr-t->input_packet));
			memcpy(t->input_packet_ptr,&packet[i+3],len-3);
			t->input_packet_ptr+=len-3;
			t->input_packet_bytes_missing-=len-3;
			if (t->input_packet_bytes_missing==0) {
				  //printf("Long Teensy command complete, decoding\n");
				  t->expect_fragment_id=0;
				  decode_packet(t,t->input_packet,t->input_packet[0]);
			} else {
				if (t->input_packet_bytes_missing <0) {
					printf("Mismatch in frame length, packet fragments invalid\n");
					t->expect_fragment_id = 0;
					return;
				}
				t->expect_fragment_id++;
				//printf("%d bytes still missing, expecting more command fragments (ptr=%d)\n",
				//		  t->input_packet_bytes_missing, (int)(t->input_packet_ptr-t->input_packet));
			}
		}
		i += len;
	} while (i < 64);
}

void TeensyControls_update_xplane(float elapsedNotUsed)
{
	teensy_t *t;
	item_t *item;
	int i, count;
	float f;
	int floatTrunc;

	// step 1: do all commands
	for (t = TeensyControls_first_teensy; t; t = t->next) {
		for (item = t->items; item; item = item->next) {
			if (item->type != 0) continue;
			count = item->command_count;
			for (i = 0; i < count; i++) {
				switch (item->command_queue[i]) {
				  case 0x04: // command begin
					//XPLMCommandBegin(item->cmdref);
					printf("Command %s Begin\n", item->name);
					item->command_began = 1;
					break;
				  case 0x05: // command end
					//XPLMCommandEnd(item->cmdref);
					printf("Command %s End\n", item->name);
					item->command_began = 0;
					break;
				  case 0x06: // command once
					//XPLMCommandOnce(item->cmdref);
					printf("Command %s Once\n", item->name);
				}
			}
			item->command_count = 0;
		}
	}
	// step 2: write any data Teensy changed
	for (t = TeensyControls_first_teensy; t; t = t->next) {
		for (item = t->items; item; item = item->next) {
			//printf("Process item %s  val: %f\n", item->name, (float)item->intval);
			if (item->type == 1 && item->changed_by_teensy) {
				//printf("Int changed by Teensy so write %s = %d\n", item->name, item->intval);
				dataRefWrite(item->dataref, item->intval);
				item->changed_by_teensy = 0;
			} else if (item->type == 2 && item->changed_by_teensy) {
				//printf("Float changed by Teensy so write %s = %.3f\n", item->name, item->floatval);
				dataRefWrite(item->dataref, item->floatval);
				item->changed_by_teensy = 0;
			}
		}
	}
	// step 3: read all data from simulator
	for (t = TeensyControls_first_teensy; t; t = t->next) {
		for (item = t->items; item; item = item->next) {
			if (dataRefWritten(item->dataref)) {
				continue;
			}

			double value;
			switch (item->type) {
				case 0x01: // integer
					value = dataRefRead(item->dataref);
					if (value == MAXINT) {
						item->intval = MAXINT;
						item->intval_remote = MAXINT;
					}
					else {
						item->intval = value;
					}
					//if (item->intval != item->intval_remote) {
					//	printf("Sim int %s changed from %d to %d\n", item->name, item->intval_remote, item->intval);
					//}
					break;

					case 0x02: // float
						value = dataRefRead(item->dataref);
						if (value == MAXINT) {
							item->floatval_remote = MAXINT;
						}
						else {
							item->floatval = value;
						}

						//if (item->floatval != item->floatval_remote) {
						//	printf("Sim float %s changed from %.3f to %.3f\n", item->name, item->floatval_remote, item->floatval);
						//}
						break;
				 
				case 0x04: // string
					printf("Read String from sim %s - Scott not implemented\n", item->name);
					break;
			}
		}
	}
}


// output any items where our copy is different than Teensy's remote copy
// elapsed is time in seconds since previous output
// flags = 1 upon enable event
// flags = 2 upon disable event
//
void TeensyControls_output(float elapsedNotUsed, int flags)
{
	teensy_t* t;
	item_t* item;
	uint8_t buf[64], enable_state = 2, en;
	int32_t i32;

	if (flags == 1) {
		enable_state = 1;
	}
	else if (flags == 2) {
		enable_state = 3;
	}
	for (t = TeensyControls_first_teensy; t; t = t->next) {
		en = enable_state;
		if (en == 2 && t->unknown_id_heard) {
			en = 1;
			t->unknown_id_heard = 0;
		}
		buf[0] = 4;
		buf[1] = 3;
		buf[2] = en;
		buf[3] = 0;

#ifdef DEBUG
		if (en == 1) {
			printf("Output: enable and send IDs\n");
		}
		if (en == 3) {
			printf("Output: disable\n");
		}
#endif

		if (!output_data(t, buf, 4)) break;
		if (t->frames_without_id++ <= ID_FRAME_TIMEOUT) break;  // don't send data until 5 frames after the last received id

		//printf("Send data to Teensy\n");
		for (item = t->items; item; item = item->next) {
			//if (item->type == 1) {
			//	printf("Int to Teensy: %s = %d -> %d\n", item->name, item->intval_remote, item->intval);
			//}
			//else if (item->type == 2) {
			//	printf("Float to Teensy: %s = %.3f -> %.3f\n", item->name, item->floatval_remote, item->floatval);
			//}
			if (item->type == 1 && item->intval != item->intval_remote) {
#ifdef DEBUG
				printf("Int to Teensy: %s = %d\n", item->name, item->intval);
#endif
				i32 = item->intval;
				buf[0] = 10;			// length
				buf[1] = 2;			// 2 = write data
				buf[2] = item->id & 255;	// ID
				buf[3] = item->id >> 8;		// ID
				buf[4] = 1;			// type, 1 = integer
				buf[5] = 0;			// reserved
				buf[6] = i32 & 255;
				buf[7] = (i32 >> 8) & 255;
				buf[8] = (i32 >> 16) & 255;
				buf[9] = (i32 >> 24) & 255;
				if (!output_data(t, buf, 10)) {
					printf("Failed to output data\n");
					break;
				}
				item->intval_remote = item->intval;
			} else if (item->type == 2 && item->floatval != item->floatval_remote) {
#ifdef DEBUG
				printf("Float to Teensy: %s = %.3f\n", item->name, item->floatval);
#endif
				//i32 = *(int32_t *)((char *)(&(item->floatval)));
				float floatval = item->floatval;	// Convert double to float
				i32 = bytes2in32(&floatval);
				buf[0] = 10;			// length
				buf[1] = 2;			// 2 = write data
				buf[2] = item->id & 255;	// ID
				buf[3] = item->id >> 8;		// ID
				buf[4] = 2;			// type, 2 = float
				buf[5] = 0;			// reserved
				buf[6] = i32 & 255;
				buf[7] = (i32 >> 8) & 255;
				buf[8] = (i32 >> 16) & 255;
				buf[9] = (i32 >> 24) & 255;
				if (!output_data(t, buf, 10)) {
					printf("Failed to output data\n");
					break;
				}
				item->floatval_remote = item->floatval;
			} else if (item->type == 4) {
				int update = item->stringval_len != item->stringval_remote_len;
				if (update) {
//					printf("String update on item %s due to length change. Old value: %d, New value: %d\n",
//						item->name, item->stringval_remote_len, item->stringval_len);
				} else {
					update = memcmp(item->stringval, item->stringval_remote,STRING_MAX_LEN);
					if (update) {
//						printf("String update on item %s due to data change. Old data: %s, New data: %s\n",
//							item->name, item->stringval_remote, item->stringval);
					}
				}
				if (update) {
					printf("String to Teensy: %s = %s\n", item->name, item->stringval);
					buf[0] = item->stringval_len+6;
					buf[1] = 2;
					buf[2] = item->id & 255;
					buf[3] = item->id >> 8;
					buf[4] = 4;
					buf[5] = 0;
					memcpy(buf+6,item->stringval,item->stringval_len);
					if (!output_data(t, buf, 64)) {
						printf("Failed to output data\n");
						break;
					}
					memcpy(item->stringval_remote, item->stringval, item->stringval_len);
					item->stringval_remote_len = item->stringval_len;
				}
			}
		}
		output_flush(t);
	}
}

static void output_packet(teensy_t *t)
{
	int len = t->output_packet_len;
	if (len < 64) memset(t->output_packet + len, 0, 64 - len);
	TeensyControls_output_store(t, t->output_packet);
	t->output_packet_len = 0;
}

static int output_data(teensy_t *t, const uint8_t *data, int datalen)
{
	if (!data || datalen <= 0 || datalen > 64) return 0;
	if (t->output_packet_len + datalen > 64) output_packet(t);
	memcpy(t->output_packet + t->output_packet_len, data, datalen);
	t->output_packet_len += datalen;
	return 1;
}

static void output_flush(teensy_t *t)
{
	if (t->output_packet_len > 0) output_packet(t);
}
