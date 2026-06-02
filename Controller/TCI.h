#ifndef TCI_h
#define TCI_h

#define TCI_ESP32_LIB_VER "EESDR3 - 1.9"
#define N_MAX_RTX 2
#define WS_LIST_SIZE 20
#define MESSAGE_LEN 90

#define NUM_OF_CAT_PORT 2
#define CAT_PORT_BUFFER_LEN 80
#define CAT_END_CMD ';'

// Gate the verbose "NNN - <msg> - Unhandled message!" Serial.printf in
// TCI::parse_message(). The upstream library prints every TCI frame it
// does not have an explicit handler for, which floods the console for
// projects that consume only a subset of the API (this BPF controller
// only uses vfo / modulation / trx events). Define to 1 before including
// "TCI.h" to opt back in. Defaults to 0 (silent) in this bundled copy.
#ifndef TCI_LOG_UNHANDLED
#define TCI_LOG_UNHANDLED 0
#endif

#include "Arduino.h"
#include <WebSocketsClient.h>
#if defined(ESP8266)
  // ESP8266 port (AntennaSwitchController): no native WiFi.h; use core header.
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif
#include "RTX.h"

extern "C" {
	typedef void (*eventHandlerFunction)(void);
}

extern "C" {
	typedef void (*eventHandlerRigFunction)(const int senderRig);
}

extern "C" {
	typedef void (*eventHandlerRigVfoFunction)(const int senderRig, const int senderVfo);
}

extern "C" {
	typedef void (*eventHandleTxSensorsFunction)(const int senderRig, const float mic_level,
	              const float rms_power_out, const float peak_power_out, const float swr);
}

struct ws_message
{   
   unsigned int len;
   char ws_data [MESSAGE_LEN] = {};
};

struct spot
{      
   char callsing [20];
   unsigned int frequency;
   int rtxId;
   int vfoId;
};

struct cat
{
	Stream *port;
	int rtxId;
};

class TCI
{
  public:
    TCI();
	//~TCI();

	void test();
			
	void set_host(char *host_name);
	void set_port(int value);
	void set_iaru_region(int value);
	void connect();
	void disconnect();
	bool connected();
	// ESP8266 cooperative port: call from loop() to pump the WebSocket and
	// drain/parse one queued message. Replaces the ESP32 FreeRTOS tasks.
	void process();
	void attach_conn_disc_event(eventHandlerFunction _eventHandler);
	void set_cat_port(int portId, Stream *port_name, int rtxId);
	RTX rtx[N_MAX_RTX];	

	//
	// ********************** INITIALIZATIONS COMMANDS ***********************
	//

	// ===== VFO_LIMITS =====
	void attach_vfo_limits_event(eventHandlerRigVfoFunction _eventHandler);
	int get_vfo_low_limit();
	int get_vfo_high_limit();
	
	// ===== IF_LIMITS =====
	void attach_if_limits_event(eventHandlerRigVfoFunction _eventHandler);
	int get_if_low_limit();
	int get_if_high_limit();

	// ===== TRX_COUNT =====
	void attach_trx_count_event(eventHandlerRigFunction _eventHandler);
	int get_trx_count();

	// ===== CHANNELS_COUNT =====
	void attach_channels_count_event(eventHandlerRigFunction _eventHandler);
	int get_channels_count();
	
	// ===== DEVICE =====
	void attach_device_event(eventHandlerFunction _eventHandler);
	void get_device(char *result_buffer, int *len, int buf_size);

	// ===== RECEIVE ONLY =====
	void attach_receive_only_event(eventHandlerFunction _eventHandler);
	bool is_receive_only();	

	// ===== MODULATIONS_LIST =====
	void attach_modulations_list_event(eventHandlerFunction _eventHandler);
	void get_modulations_list(char *result_buffer, int *len, int buf_size);

	// ===== PROTOCOL =====
	void attach_protocol_event(eventHandlerFunction _eventHandler);
	void get_protocol(char *result_buffer, int *len, int buf_size);

	// ===== READY =====
	void attach_ready_event(eventHandlerFunction _eventHandler);
	bool is_ready();

	//
	// ********************** BIDIRECTIONAL CONTROL COMMANDS ***********************
	//	
	
	// ===== START =====
	bool is_started();
	void attach_started_event(eventHandlerFunction _eventHandler);
	void start_device();
	
	// ===== STOP =====
	//bool is_started();
	void attach_stopped_event(eventHandlerFunction _eventHandler);
	void stop_device();

	// ===== DDS =====
	void attach_dds_event(eventHandlerRigFunction _eventHandler);
	void set_dds(int rtxId, int freq);
	
	// ===== IF =====
	void attach_if_event(eventHandlerRigVfoFunction _eventHandler);
	void set_if(int rtxId, int vfoId, int freq);

	// ===== VFO =====
	void attach_vfo_event(eventHandlerRigVfoFunction _eventHandler);
	void set_vfo(int rtxId, int vfoId, int freq);

	// ===== MODULATION =====
	void attach_modulation_event(eventHandlerRigFunction _eventHandler);
	void set_modulation(int rtxId, char *modulation);

	// ===== TRX =====
	enum trx_mode { TRX_CHECK, TRX_TRUE, TRX_FALSE, TRX_TRUE_TCI, TRX_FALSE_TCI };
	void attach_trx_event(eventHandlerRigFunction _eventHandler);
	void set_trx(int rtxId, trx_mode mode);

	// ===== TUNE =====
	enum tune_mode { TUNE_CHECK, TUNE_TRUE, TUNE_FALSE };
	void attach_tune_event(eventHandlerRigFunction _eventHandler);
	void set_tune(int rtxId, tune_mode mode);

	// ===== DRIVE =====
	void attach_drive_event(eventHandlerRigFunction _eventHandler);
	void set_drive(int rtxId, int power);

	// ===== TUNE_DRIVE =====
	void attach_tune_drive_event(eventHandlerRigFunction _eventHandler);
	void set_tune_drive(int rtxId, int power);

	// ===== RIT_ENABLE =====
	void attach_rit_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rit_enable(int rtxId, bool value);

	// ===== XIT_ENABLE =====
	void attach_xit_enable_event(eventHandlerRigFunction _eventHandler);
	void set_xit_enable(int rtxId, bool value);

	// ===== SPLIT_ENABLE =====
	void attach_split_enable_event(eventHandlerRigFunction _eventHandler);
	void set_split_enable(int rtxId, bool value);

	// ===== RIT_OFFSET =====
	void attach_rit_offset_event(eventHandlerRigFunction _eventHandler);
	void set_rit_offset(int rtxId, int power);

	// ===== XIT_OFFSET =====
	void attach_xit_offset_event(eventHandlerRigFunction _eventHandler);
	void set_xit_offset(int rtxId, int power);	

	// ===== RX_CHANNEL_ENABLE =====
	void attach_rx_channel_enable_event(eventHandlerRigVfoFunction _eventHandler);
	void set_rx_channel_enable(int rtxId, int vfoId, bool enable);

	// ===== RX_FILTER_BAND =====
	void attach_rx_filter_band_event(eventHandlerRigFunction _eventHandler);
	void set_rx_filter_band(int rtxId, int lower, int top);

	// ===== CW_MACROS_SPEED =====
	void attach_cw_macros_speed_event(eventHandlerFunction _eventHandler);
	int get_cw_macros_speed();
	void set_cw_macros_speed(int speed);

	// ===== CW_MACROS_DELAY =====
	void attach_cw_macros_delay_event(eventHandlerFunction _eventHandler);
	int get_cw_macros_delay();
	void set_cw_macros_delay(int milliseconds);

	// ===== CW_KEYER_SPEED =====
	void attach_cw_keyer_speed_event(eventHandlerFunction _eventHandler);
	int get_cw_keyer_speed();
	void set_cw_keyer_speed(int speed);

	// ===== VOLUME =====
	void attach_volume_event(eventHandlerFunction _eventHandler);
	int get_volume();
	void set_volume(int db_level);

	// ===== MUTE =====
	void attach_mute_event(eventHandlerFunction _eventHandler);
	void set_mute(bool value);
	bool is_mute();
	
	// ===== RX_MUTE =====
	void attach_rx_mute_event(eventHandlerRigFunction _eventHandler);
	void set_rx_mute(int rtxId, bool value);

	// ===== RX_VOLUME =====
	void attach_rx_volume_event(eventHandlerRigVfoFunction _eventHandler);
	void set_rx_volume(int rtxId, int vfoId, int db_level);

	// ===== RX_BALANCE =====
	void attach_rx_balance_event(eventHandlerRigVfoFunction _eventHandler);
	void set_rx_balance(int rtxId, int vfoId, int db_level);

	// ===== MON_VOLUME =====
	void attach_mon_volume_event(eventHandlerFunction _eventHandler);
	int get_mon_volume();
	void set_mon_volume(int db_level);

	// ===== MON_ENABLE =====
	void attach_mon_enable_event(eventHandlerFunction _eventHandler);
	void set_mon_enable(bool value);
	bool is_mon_enable();

	// ===== AGC_MODE =====
	void attach_agc_mode_event(eventHandlerRigFunction _eventHandler);
	void set_agc_mode(int rtxId, int mode); /* 0:off - 1-fast - 2-normal */

	// ===== AGC_GAIN =====
	void attach_agc_gain_event(eventHandlerRigFunction _eventHandler);
	void set_agc_gain(int rtxId, int db_level);

	// ===== RX_NB_ENABLE =====
	void attach_rx_nb_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_nb_enable(int rtxId, bool value);

	// ===== RX_NB_PARAM =====
	void attach_rx_nb_param_event(eventHandlerRigFunction _eventHandler);
	void set_rx_nb_param(int rtxId, int triggering_threshold, int pulse_duration);

	// ===== RX_BIN_ENABLE =====
	void attach_rx_bin_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_bin_enable(int rtxId, bool value);

	// ===== RX_NR_ENABLE =====
	void attach_rx_nr_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_nr_enable(int rtxId, bool value);

	// ===== RX_ANC_ENABLE =====
	void attach_rx_anc_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_anc_enable(int rtxId, bool value);

	// ===== RX_ANF_ENABLE =====
	void attach_rx_anf_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_anf_enable(int rtxId, bool value);

	// ===== RX_APF_ENABLE =====
	void attach_rx_apf_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_apf_enable(int rtxId, bool value);

	// ===== RX_DSE_ENABLE =====
	void attach_rx_dse_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_dse_enable(int rtxId, bool value);

	// ===== RX_NF_ENABLE =====
	void attach_rx_nf_enable_event(eventHandlerRigFunction _eventHandler);
	void set_rx_nf_enable(int rtxId, bool value);

	// ===== LOCK =====
	void attach_lock_event(eventHandlerRigFunction _eventHandler);
	void set_lock(int rtxId, bool value);

	// ===== SQL_ENABLE =====
	void attach_sql_enable_event(eventHandlerRigFunction _eventHandler);
	void set_sql_enable(int rtxId, bool value);

	// ===== SQL_LEVEL =====
	void attach_sql_level_event(eventHandlerRigFunction _eventHandler);
	void set_sql_level(int rtxId, int db_level);

	//
	// ********************** UNIDIRECTIONAL CONTROL COMMANDS ***********************
	//	

	// ===== TX_ENABLE =====
	void attach_tx_enable_event(eventHandlerRigFunction _eventHandler);

	// ===== CW_MACROS_SPEED_UP =====
	void set_cw_macros_speed_up(int speed);

	// ===== CW_MACROS_SPEED_DOWN =====
	void set_cw_macros_speed_down(int speed);

	// ===== SPOT =====
	void set_spot(char *callsign, char *mode, unsigned int frequency, unsigned int argb_color);

	// ===== SPOT_DELETE =====
	void delete_spot(char *callsign);

	// ===== IQ_SAMPLERATE =====
	void attach_iq_samplerate_event(eventHandlerFunction _eventHandler);
	void set_iq_samplerate(int value);
	int get_iq_samplerate();

	// ===== AUDIO_SAMPLERATE =====
	void attach_audio_samplerate_event(eventHandlerFunction _eventHandler);
	void set_audio_samplerate(int value);
	int get_audio_samplerate();

	// ===== IQ_START/IQ_STOP =====
	void attach_iq_start_stop_event(eventHandlerRigFunction _eventHandler);
	void iq_start(int rtxId);
    void iq_stop(int rtx_id);

	// ===== AUDIO_START/AUDIO_STOP =====
	void attach_audio_start_stop_event(eventHandlerRigFunction _eventHandler);
	void audio_start(int rtxId);
    void audio_stop(int rtx_id);

	// ===== LINE_OUT_START/LINE_OUT_STOP =====
	void attach_line_out_start_stop_event(eventHandlerRigFunction _eventHandler);
	void line_out_start(int rtxId);
    void line_out_stop(int rtx_id);

	// ===== LINE_OUT_RECORDER_START =====
	void attach_line_out_recorder_start_event(eventHandlerRigFunction _eventHandler);
	void line_out_recorder_start(int rtxId,int duration);

	// ===== LINE_OUT_RECORDER_SAVE =====
	void attach_line_out_recorder_save_event(eventHandlerRigFunction _eventHandler);
	void line_out_recorder_save(int rtxId,char *filename);

	// ===== LINE_OUT_RECORDER_BREAK =====
	void attach_line_out_recorder_break_event(eventHandlerRigFunction _eventHandler);
	void line_out_recorder_break(int rtxId);

	// ===== SPOT_CLEAR =====
	void spot_clear();

	//
	// ********************** NOTIFICATIONS COMMANDS ***********************
	//	
	
	// ===== CLICKED_ON_SPOT =====
	void attach_clicked_on_spot_event(eventHandlerFunction _eventHandler);
	void attach_rx_clicked_on_spot_event(eventHandlerFunction _eventHandler);
	spot get_clicked_spot();


	// ===== TX_FOOTSWITCH =====
	void attach_tx_footswitch_event(eventHandlerRigFunction _eventHandler);
	void set_tx_footswitch(int rtxId, bool value); 

	// ===== TX_FREQUENCY =====
	void attach_tx_frequency_event(eventHandlerFunction _eventHandler);
	unsigned int get_tx_frequency();
	
	// ===== APP_FOCUS =====
	void attach_app_focus_event(eventHandlerFunction _eventHandler);
	bool is_app_focus();

	// ===== SET_IN_FOCUS =====
	void set_in_focus();

	// ===== KEYER =====
	void set_keyer(int rtxId, bool value);

	// ===== RX_SENSORS_ENABLE =====
	void set_rx_sensors_enable(bool enable, int interval=100); 
	bool is_rx_sensors_enable();
	int get_rx_sensors_interval();

	// ===== RX_SENSORS =====
	void attach_rx_sensors_event(eventHandlerRigVfoFunction _eventHandler);

	// ===== TX_SENSORS_ENABLE =====
	void set_tx_sensors_enable(bool enable, int interval=100); 
	bool is_tx_sensors_enable();
	int get_tx_sensors_interval();

	// ===== TX_SENSORS =====
	void attach_tx_sensors_event(eventHandleTxSensorsFunction _eventHandler);
	
	// ===== AUDIO_STREAM_SAMPLE_TYPE =====
	int get_audio_stream_sample_type();
	void set_audio_stream_sample_type(int type);
	void attach_audio_stream_sample_type_event(eventHandlerRigFunction _eventHandler);

	// ===== AUDIO_STREAM_CHANNELS =====
	int get_audio_stream_channels();
	void set_audio_stream_channels(int val);
	void attach_audio_stream_channels_event(eventHandlerRigFunction _eventHandler);

	// ===== AUDIO_STREAM_SAMPLES =====
	int get_audio_stream_samples();
	void set_audio_stream_samples(int val);
	void attach_audio_stream_samples_event(eventHandlerRigFunction _eventHandler);

	// ===== DIGL_OFFSET =====
	int get_digl_offset();
	void set_digl_offset(int val);
	void attach_digl_offset_event(eventHandlerRigFunction _eventHandler);

	// ===== DIGU_OFFSET =====
	void attach_digu_offset_event(eventHandlerRigFunction _eventHandler);
	int get_digu_offset();
	void set_digu_offset(int val);


  private:

	char *host; 
	uint16_t port;
	uint8_t iaru_region;

	char SerialInBuffer[NUM_OF_CAT_PORT][CAT_PORT_BUFFER_LEN];
	char SerialOutBuffer[NUM_OF_CAT_PORT][CAT_PORT_BUFFER_LEN];
	int curInBufIndex[NUM_OF_CAT_PORT];
	cat cat_port[NUM_OF_CAT_PORT];
#if !defined(ESP8266)
	TaskHandle_t cat_task_handle;
	static void cat_task(void * parameters);
#endif
	void send_cat_IF(int rtxId);


	struct ws_message ws_messages[WS_LIST_SIZE];
	bool ws_buffer_full;
	int write_index;                 
	int read_index;

#if !defined(ESP8266)
	TaskHandle_t web_socket_reader_task_handle;
	static void ws_reader_task(void * parameters);
#endif
	void put_messages(char *data, unsigned int length);
#if !defined(ESP8266)
	TaskHandle_t web_socket_event_task_handle;
	static void ws_event_task(void * parameters);
#endif
	void parse_message(unsigned int length);
	char incoming_message[MESSAGE_LEN];
	char outgoing_message[MESSAGE_LEN];

	WebSocketsClient webSocket;
	volatile bool _connected;
	void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
	void hexdump(const void *mem, uint32_t len, uint8_t cols);
	eventHandlerFunction do_conn_disc_event = NULL;	

	char tmp_bool_buffer[10];
	bool eval_bool_buf();

	void reset();
	void send_message();
	
	//int smeterTimeout;
	//unsigned long previousSwrCmd = 0;	

	//
	// ********************** INITIALIZATIONS COMMANDS ***********************
	//

	// ===== VFO_LIMITS =====
	int vfo_limits[2];
	eventHandlerRigVfoFunction do_vfo_limits_event = NULL;

	// ===== IF_LIMITS =====
	int if_limits[2];
	eventHandlerRigVfoFunction do_if_limits_event = NULL;

	// ===== TRX_COUNT =====
	int trx_count;
	eventHandlerRigFunction do_trx_count_event = NULL;

	// ===== CHANNELS_COUNT =====
	int channels_count;
	eventHandlerRigFunction do_channels_count_event = NULL;

	// ===== CHANNELS_COUNT =====
	char device[20];
	eventHandlerFunction do_device_event = NULL;

	// ===== RECEIVE_ONLY =====
	bool receive_only;	
	eventHandlerFunction do_receive_only_event = NULL;

	// ===== MODULATIONS_LIST =====
	char modulations_list[90];
	eventHandlerFunction do_modulations_list_event = NULL;

	// ===== PROTOCOL =====
	char protocol[20];
	eventHandlerFunction do_protocol_event = NULL;

	// ===== READY =====
	bool ready;
	eventHandlerFunction do_ready_event = NULL;

	//
	// ********************** BIDIRECTIONAL CONTROL COMMANDS ***********************
	//

	// ===== START =====
	bool started;
	eventHandlerFunction do_started_event = NULL;

	// ===== STOP =====
	//bool started; this command is based on the started property
	eventHandlerFunction do_stopped_event = NULL;

	// ===== DDS =====
	eventHandlerRigFunction do_dds_event = NULL;

	// ===== IF =====
	eventHandlerRigVfoFunction do_if_event = NULL;

	// ===== VFO =====
	eventHandlerRigVfoFunction do_vfo_event = NULL;

	// ===== MODULATION =====
	eventHandlerRigFunction do_modulation_event=NULL;

	// ===== TRX =====
	eventHandlerRigFunction do_trx_event=NULL;

	// ===== TUNE =====
	eventHandlerRigFunction do_tune_event=NULL;

	// ===== DRIVE =====
	eventHandlerRigFunction do_drive_event=NULL;

	// ===== TUNE_DRIVE =====
	eventHandlerRigFunction do_tune_drive_event=NULL;

	// ===== RIT_ENABLE =====
	eventHandlerRigFunction do_rit_enable_event=NULL;

	// ===== XIT_ENABLE =====
	eventHandlerRigFunction do_xit_enable_event=NULL;

	// ===== SPLIT_ENABLE =====
	eventHandlerRigFunction do_split_enable_event=NULL;

	// ===== RIT_OFFSET =====
	eventHandlerRigFunction do_rit_offset_event=NULL;
	
	// ===== XIT_OFFSET =====
	eventHandlerRigFunction do_xit_offset_event=NULL;
	
	// ===== RX_CHANNEL_ENABLE =====
	eventHandlerRigVfoFunction do_rx_channel_enable_event = NULL;
	
	// ===== RX_FILTER_BAND =====
	eventHandlerRigFunction do_rx_filter_band_event=NULL;

	// ===== CW_MACROS_SPEED =====
	int cw_macros_speed;
	eventHandlerFunction do_cw_macros_speed_event=NULL;
	
	// ===== CW_MACROS_DELAY =====
	int cw_macros_delay;
	eventHandlerFunction do_cw_macros_delay_event=NULL;
		
	// ===== CW_KEYER_SPEED =====
	int cw_keyer_speed;
	eventHandlerFunction do_cw_keyer_speed_event=NULL;
	
	// ===== VOLUME =====
	int volume;
	eventHandlerFunction do_volume_event=NULL;

	// ===== MUTE =====
	bool mute;	
	eventHandlerFunction do_mute_event = NULL;

	// ===== RX_MUTE =====
	bool rx_mute;	
	eventHandlerRigFunction do_rx_mute_event = NULL;
	
	// ===== RX_VOLUME =====
	eventHandlerRigVfoFunction do_rx_volume_event=NULL;

	// ===== RX_BALANCE =====
	eventHandlerRigVfoFunction do_rx_balance_event=NULL;

	// ===== MON_VOLUME =====
	int mon_volume;
	eventHandlerFunction do_mon_volume_event=NULL;

	// ===== MON_ENABLE =====
	bool mon_enable;	
	eventHandlerFunction do_mon_enable_event = NULL;

	// ===== AGC_MODE =====
	eventHandlerRigFunction do_agc_mode_event=NULL;
	
	// ===== AGC_GAIN =====
	eventHandlerRigFunction do_agc_gain_event=NULL;

	// ===== RX_NB_ENABLE =====
	eventHandlerRigFunction do_rx_nb_enable_event=NULL;

	// ===== RX_NB_PARAM =====
	eventHandlerRigFunction do_rx_nb_param_event=NULL;

	// ===== RX_BIN_ENABLE =====
	eventHandlerRigFunction do_rx_bin_enable_event=NULL;

	// ===== RX_NR_ENABLE =====
	eventHandlerRigFunction do_rx_nr_enable_event=NULL;

	// ===== RX_ANC_ENABLE =====
	eventHandlerRigFunction do_rx_anc_enable_event=NULL;

	// ===== RX_ANF_ENABLE =====
	eventHandlerRigFunction do_rx_anf_enable_event=NULL;

	// ===== RX_APF_ENABLE =====
	eventHandlerRigFunction do_rx_apf_enable_event=NULL;

	// ===== RX_DSE_ENABLE =====
	eventHandlerRigFunction do_rx_dse_enable_event=NULL;

	// ===== RX_NF_ENABLE =====
	eventHandlerRigFunction do_rx_nf_enable_event=NULL;

	// ===== LOCK =====
	eventHandlerRigFunction do_lock_event=NULL;

	// ===== SQL_ENABLE =====
	eventHandlerRigFunction do_sql_enable_event=NULL;

	// ===== SQL_LEVEL =====
	eventHandlerRigFunction do_sql_level_event=NULL;
	
	//
	// ********************** UNIDIRECTIONAL CONTROL COMMANDS ***********************
	//

	// ===== TX_ENABLE =====
	eventHandlerRigFunction do_tx_enable_event = NULL;

	// ===== IQ_SAMPLERATE =====
	int iq_samplerate;
	eventHandlerFunction do_iq_samplerate_event=NULL;

	// ===== AUDIO_SAMPLERATE =====
	int audio_samplerate;
	eventHandlerFunction do_audio_samplerate_event=NULL;

	// ===== IQ_START/IQ_STOP =====
	eventHandlerRigFunction do_iq_start_stop_event=NULL;

	// ===== AUDIO_START/AUDIO_STOP =====
	eventHandlerRigFunction do_audio_start_stop_event=NULL;

	// ===== LINE_OUT_START/LINE_OUT_STOP =====
	eventHandlerRigFunction do_line_out_start_stop_event=NULL;

	// ===== LINE_OUT_RECORDER_START =====
	eventHandlerRigFunction do_line_out_recorder_start_event=NULL;

	// ===== LINE_OUT_RECORDER_SAVE =====
	eventHandlerRigFunction do_line_out_recorder_save_event=NULL;

	// ===== LINE_OUT_RECORDER_BREAK =====
	eventHandlerRigFunction do_line_out_recorder_break_event=NULL;
	
	// ===== AUDIO_STREAM_SAMPLE_TYPE =====
	int audio_stream_sample_type;
	eventHandlerRigFunction do_audio_stream_sample_type_event=NULL;

	// ===== AUDIO_STREAM_CHANNELS =====
	int audio_stream_channels_count; 
	eventHandlerRigFunction do_audio_stream_channels_event=NULL;

	// ===== AUDIO_STREAM_SAMPLES =====
	int audio_stream_samples; 
	eventHandlerRigFunction do_audio_stream_samples_event=NULL;

	// ===== DIGL_OFFSET =====
	int digl_offset; 
	eventHandlerRigFunction do_digl_offset_event=NULL;

	// ===== DIGU_OFFSET =====
	int digu_offset; 
	eventHandlerRigFunction do_digu_offset_event=NULL;

	//
	// ********************** NOTIFICATIONS COMMANDS ***********************
	//

	// ===== CLICKED_ON_SPOT =====
	spot clicked_spot;
	eventHandlerFunction do_clicked_on_spot_event = NULL;

	// ===== RX_CLICKED_ON_SPOT =====
	//spot clicked_spot;
	eventHandlerFunction do_rx_clicked_on_spot_event = NULL;


	// ===== TX_FOOTSWITCH =====
	eventHandlerRigFunction do_tx_footswitch_event = NULL;

	// ===== TX_FREQUENCY =====
	int tx_frequency;
	eventHandlerFunction do_tx_frequency_event = NULL;

	// ===== APP_FOCUS =====
	bool app_focus;
	eventHandlerFunction do_app_focus_event=NULL;

	// ===== RX_SENSORS_ENABLE =====
	bool rx_sensors_enable;
	int rx_sensors_interval;

	// ===== RX_SENSORS =====
	eventHandlerRigVfoFunction do_rx_sensors_event=NULL;

	// ===== RX_SENSORS_ENABLE =====
	bool tx_sensors_enable;
	int tx_sensors_interval;

	// ===== TX_SENSORS =====
	eventHandleTxSensorsFunction do_tx_sensors_event=NULL;

};

#endif