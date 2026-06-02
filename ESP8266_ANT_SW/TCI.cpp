#include "TCI.h"

TCI::TCI() {
#if !defined(ESP8266)
	web_socket_reader_task_handle = NULL;
#endif
	_connected = false;
	for (int i=0;i<NUM_OF_CAT_PORT;i++) {
		this->cat_port[i].port = NULL; this->cat_port[i].rtxId = -1;	
	}
	reset();
}

void TCI::test()
{
	Serial.printf("\n\n\nTCI->hostname: %s\n",host);
	Serial.printf("TCI->port: %d\n",port);
	Serial.printf("TCI->iaru_region: %d\n",iaru_region);
	Serial.printf("TCI->connected: %d\n",_connected);	
	Serial.printf("WiFi->isConnected(): %d\n",WiFi.isConnected());		
    Serial.printf("\nread_index: %02d - write_index: %02d\n",read_index, write_index);
    /*Serial.printf("=== LIST CONTENT =====================\n");
    for (int i=0; i<WS_LIST_SIZE; i++)
    {
      Serial.printf("%02d - %03u - %s\n", i, ws_messages[i].len, ws_messages[i].ws_data);
    }
    Serial.printf("======================================\n");*/
	Serial.printf("\n");		
}

void TCI::set_host(char *host_name)
{
	this->host = host_name;
}

void TCI::set_port(int value)
{
	this->port = (uint16_t)value;
}

void TCI::set_iaru_region(int value)
{
	this->iaru_region = (uint8_t)value;
}

void TCI::connect() {

	if (!WiFi.isConnected())
	{
		Serial.printf("WiFi disconnected - unable to connect TCI-Host\n");
		return;
	}

	if (_connected)
		webSocket.disconnect();
		
	_connected = false;
	ws_buffer_full = false;
	write_index = 0;                 
	read_index = 0;

	webSocket.begin(host, port, "/");
	// event handler
	
	webSocket.onEvent([&](WStype_t t, uint8_t * p, size_t l) {
		this->webSocketEvent(t, p, l);
	});
	
	// try ever 5000 again if connection has failed
	webSocket.setReconnectInterval(5000);

#if !defined(ESP8266)
	if (web_socket_reader_task_handle == NULL)
	{
		int app_cpu = xPortGetCoreID();
		BaseType_t rc_proc_task;
		rc_proc_task = xTaskCreatePinnedToCore(
			this->ws_reader_task,
			"ws_reader_task",
			4096,
			(void*)this,
			1,
			&web_socket_reader_task_handle,
			app_cpu);
		assert(rc_proc_task == pdPASS);
	} else {
		vTaskResume(web_socket_reader_task_handle);
	}

	if (web_socket_event_task_handle == NULL)
	{
		int app_cpu = xPortGetCoreID();
		BaseType_t rce_proc_task;
		rce_proc_task = xTaskCreatePinnedToCore(
			this->ws_event_task,
			"event_task",
			4096,
			(void*)this,
			1,
			&web_socket_event_task_handle,
			app_cpu);
		assert(rce_proc_task == pdPASS);
	} else {
		vTaskResume(web_socket_event_task_handle);
	}
#endif // !ESP8266

	//Empty serial buffer
	for (int i=0; i<NUM_OF_CAT_PORT;i++) {
		if (cat_port[i].rtxId>=0) {
			while (cat_port[i].port->available()) {
				cat_port[i].port->read();
			}
		}
	}

#if !defined(ESP8266)
	if (cat_task_handle == NULL)
	{
		int app_cpu = xPortGetCoreID();
		BaseType_t rcc_proc_task;
		rcc_proc_task = xTaskCreatePinnedToCore(
			this->cat_task,
			"cat_task",
			2048,
			(void*)this,
			1,
			&cat_task_handle,
			app_cpu);
		assert(rcc_proc_task == pdPASS);
	} else {
		vTaskResume(cat_task_handle);
	}
#endif // !ESP8266

}

// ESP8266 cooperative replacement for the FreeRTOS reader + event tasks.
// Pump the WebSocket, then drain and parse at most one queued frame.
void TCI::process() {
	webSocket.loop();

	if (read_index == write_index && (!ws_buffer_full))
		return;  // ring buffer empty

	ws_buffer_full = false;
	memset(incoming_message, 0, MESSAGE_LEN);
	strncpy(incoming_message,
	        ws_messages[read_index].ws_data,
	        ws_messages[read_index].len);
	parse_message(ws_messages[read_index].len);
	ws_messages[read_index].len = 0;
	memset(ws_messages[read_index].ws_data, 0, MESSAGE_LEN);
	read_index = (read_index + 1) % WS_LIST_SIZE;
}

void TCI::webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

	switch(type) {
		case WStype_DISCONNECTED:
		    if (_connected) {
 				//Serial.printf("[WSc] Disconnected!\n");
				reset();
				_connected = false;
				if (do_conn_disc_event != NULL)
					do_conn_disc_event();
			}			
			break;
		case WStype_CONNECTED:
			//Serial.printf("[WSc] Connected to url: %s\n", payload);
            _connected = true;
			if (do_conn_disc_event != NULL)
				do_conn_disc_event();
			// send message to server when Connected
			// webSocket.sendTXT("Connected");
			break;
		case WStype_TEXT:
		    put_messages((char*)payload, length);
            //Serial.printf("[WSc] Time: %u - len: %03d - Text: %s\n", millis(), length, payload);			
		    //parseData(String((char *)payload));
			// send message to server
			// webSocket.sendTXT("message here");
			break;
		case WStype_BIN:
			Serial.printf("[WSc] get binary length: %u\n", length);
			hexdump(payload, length, 16);
			// send data to server
			// webSocket.sendBIN(payload, length);
			break;
		case WStype_ERROR:	
			//Serial.print("[WSc] WebSockets error\n");
		case WStype_PING:
			//Serial.printf("[WSc] Ping: %s\n", payload);
		    break;			
		case WStype_PONG:
			//Serial.printf("[WSc] Pong: %s\n", payload);
		    break;			
		case WStype_FRAGMENT_TEXT_START:
			//Serial.printf("[WSc] Fragment START: %s\n", payload);
		    break;
		case WStype_FRAGMENT_BIN_START:
			//Serial.printf("[WSc] Fragment STOP: %s\n", payload);
		    break;
		case WStype_FRAGMENT:
			//Serial.printf("[WSc] Fragment: %s\n", payload);
		    break;
		case WStype_FRAGMENT_FIN:
			//Serial.printf("[WSc] Fragment FIN: %s\n", payload);
		    break;
	}

}

void TCI::disconnect() {

	if (!WiFi.isConnected())
	{
		Serial.printf("WiFi disconnected - unable to disconnect TCI-Host\n");
		return;
	}

	//task_enabled = false;
	//web_socket_task_handle = NULL;
#if !defined(ESP8266)
	if (web_socket_reader_task_handle != NULL) {
		vTaskSuspend(web_socket_reader_task_handle);
		delay(100);
	}

	if (cat_task_handle != NULL) {
		vTaskSuspend(cat_task_handle);
		delay(100);
	}
#endif // !ESP8266

	webSocket.disconnect();
}

bool TCI::connected() {
	return _connected;
}

void TCI::attach_conn_disc_event(eventHandlerFunction _eventHandler) {
	do_conn_disc_event = _eventHandler;
}

void TCI::set_cat_port(int portId, Stream *port_name, int rtxId) {
	if (portId != 1 && portId != 2)	{
		Serial.printf("== TCI: Invalid portId:%d - Allowed values:[1,2]",portId);
		return;
	}
	if (rtxId != 0 && rtxId != 1 && rtxId != 2)	{
		Serial.printf("== TCI: Invalid rtxId:%d - Allowed values:[1,2] - 1=RX1 - 2=RX2",rtxId);
		return;
	}
	this->cat_port[portId-1].port = port_name;
	this->cat_port[portId-1].rtxId = rtxId-1;
}

#if !defined(ESP8266)
void TCI::ws_reader_task(void * parameters) {

	TCI* tci_instance = static_cast<TCI*>(parameters);

	unsigned int stack_hwm = 0, temp;

	//Serial.println("= Task process loop started\n");
	//while (tci_instance->task_enabled)
	while (1)
	{
		temp = uxTaskGetStackHighWaterMark(nullptr);

        //CHeck stacksize
        if ( !stack_hwm || temp < stack_hwm ) {
            stack_hwm = temp;
            /*Serial.printf("= WebSocket loop task has stack hwm %u\n",
                            stack_hwm);*/
        }

		tci_instance->webSocket.loop();
		//delay(1);
	}

}
#endif // !ESP8266

void TCI::put_messages(char *data, unsigned int length)
{
	/*while (xSemaphoreTake(ws_mutex,portMAX_DELAY) == pdFALSE)
	{
		//Just wait here
	}*/

	if (ws_buffer_full)
	{
		Serial.printf("=== Element discarded - LIST FULL !!!\n");
	} else {         
		ws_messages[write_index].len = length;        
		sprintf(ws_messages[write_index].ws_data,data);
		/*Serial.printf("Element: %02d - %03u - %s - added\n", 
						write_index, 
						ws_messages[write_index].len, 
						ws_messages[write_index].ws_data);*/
	
		write_index = (write_index + 1) % WS_LIST_SIZE;
		
		if (read_index == write_index)
		{
			ws_buffer_full = true;
		}  
		//xSemaphoreGive(ws_mutex);
	}   

	//Serial.printf("[WSc] Time: %u - len: %03d - Text: %s\n", millis(), length, data);

}

#if !defined(ESP8266)
void TCI::ws_event_task(void * parameters) {

	TCI* tci_instance = static_cast<TCI*>(parameters);

	unsigned int stack_hwm = 0, temp;

	//Serial.println("= Event process loop started\n");
	//while (tci_instance->task_enabled)
	while (1)
	{

		temp = uxTaskGetStackHighWaterMark(nullptr);
        
        //CHeck stacksize
        if ( !stack_hwm || temp < stack_hwm ) {
            stack_hwm = temp;
            /*Serial.printf("= Event loop task has stack hwm %u\n",
                            stack_hwm);*/
        }	

		//if (xSemaphoreTake(tci_instance->ws_mutex,0) == pdTRUE)
		//{

			// codice che legge dal buffer
			if (tci_instance->read_index == tci_instance->write_index && (!tci_instance->ws_buffer_full))
			{
				//Serial.printf("=== LIST EMPTY !!!\n");
			} else {
				tci_instance->ws_buffer_full = false;
				
				/*Serial.printf("%02d - %03u - %s\n", 
							tci_instance->read_index, 
							tci_instance->ws_messages[tci_instance->read_index].len, 
							tci_instance->ws_messages[tci_instance->read_index].ws_data);  */

				//clean buffer before set new message
				memset(tci_instance->incoming_message, 0, MESSAGE_LEN);
				strncpy(tci_instance->incoming_message, 
				        tci_instance->ws_messages[tci_instance->read_index].ws_data, 
						tci_instance->ws_messages[tci_instance->read_index].len);

				//tci_instance->parseMessage(tci_instance->incomingMessage,
				//						   tci_instance->ws_messages[tci_instance->read_index].len);
				tci_instance->parse_message(tci_instance->ws_messages[tci_instance->read_index].len);

				//Clear the buffer entry
				tci_instance->ws_messages[tci_instance->read_index].len = 0;
				memset(tci_instance->ws_messages[tci_instance->read_index].ws_data, 
				       0, MESSAGE_LEN);

				tci_instance->read_index = (tci_instance->read_index + 1) % WS_LIST_SIZE;
			}

			vTaskDelay (10);

			//xSemaphoreGive(tci_instance->ws_mutex);
			//doCat();
			//doSmeter();

		//}
		//delay(1);
	}

}
#endif // !ESP8266

/*
TCI::~TCI() {
	if (connected)
		webSocket.disconnect();
	_connected = false;
	Serial.println("Destructor invoked!");
}
*/

void TCI::reset() {
	memset(vfo_limits, 0, sizeof(vfo_limits));
	memset(if_limits, 0, sizeof(if_limits));
	trx_count = 0;
	channels_count = 0;
	memset(device, 0, 20);
	receive_only = false;
	memset(modulations_list, 0, 90);
	ready = false;
	started = false;
	memset(protocol, 0, 20);
	cw_macros_speed = 0;
	cw_macros_delay = 0;
	cw_keyer_speed = 0;
	volume = 0;
	mon_volume = 0;
	mute = 0;
	mon_enable = false;
	iq_samplerate = 0;
	audio_samplerate = 0;
	tx_frequency = 0;
	app_focus = false;
	for (int i=0;i<NUM_OF_CAT_PORT;i++) {
		memset(SerialOutBuffer[i], 0, CAT_PORT_BUFFER_LEN);
		memset(SerialInBuffer[i], 0, CAT_PORT_BUFFER_LEN);
	}
	rx_sensors_enable=false;
	rx_sensors_interval=0;
}

/*
void TCI::init() {
	catSetup();
	delay(500);
	setHost(p_tciHOST);
    setPort(p_tciPORT);
	open();
}*/

#if !defined(ESP8266)
void TCI::cat_task(void * parameters) {

	TCI* tci_instance = static_cast<TCI*>(parameters);

	unsigned int stack_hwm = 0, temp;

	//Serial.println("= Task Cat started\n");

	while (1)
	{		
		temp = uxTaskGetStackHighWaterMark(nullptr);
        
        //CHeck stacksize
        if ( !stack_hwm || temp < stack_hwm ) {
            stack_hwm = temp;
            /*Serial.printf("= Cat task has stack hwm %u\n",
                            stack_hwm);*/
        }	

		if (tci_instance->connected()) {

			for (int i=0; i<NUM_OF_CAT_PORT;i++) {

				if (tci_instance->cat_port[i].rtxId>=0) {
					
					if (tci_instance->cat_port[i].port->available()) {

						char c = tci_instance->cat_port[i].port->read();
						
						switch (c) {

							case '\n':  //ignore "New Line" character
							case '\r':  //ignore "Carriage Return" character
								break;

							case CAT_END_CMD:
								/*Serial.printf("rtx[%d].getVfo(0):%d\n",
											tci_instance->cat_port[i].rtxId,
											tci_instance->rtx[0].getVfo(0));*/						    
								tci_instance->SerialInBuffer[i][tci_instance->curInBufIndex[i]] = toupper(c);							
								tci_instance->SerialInBuffer[i][tci_instance->curInBufIndex[i]+1] = '\0';							
								memset(tci_instance->SerialOutBuffer[i], 0, CAT_PORT_BUFFER_LEN);
								tci_instance->rtx[tci_instance->cat_port[i].rtxId].getCatResponse(tci_instance->SerialOutBuffer[i],tci_instance->SerialInBuffer[i]);
								tci_instance->cat_port[i].port->printf("%s",tci_instance->SerialOutBuffer[i]);
								memset(tci_instance->SerialInBuffer[i], 0, CAT_PORT_BUFFER_LEN);
								tci_instance->curInBufIndex[i] = 0;
							break;  

							default:
								int index = tci_instance->curInBufIndex[i]++;
								if (index < CAT_PORT_BUFFER_LEN-2) {
									tci_instance->SerialInBuffer[i][index] = toupper(c);  
								} else {
									Serial.printf("== TCI: PORT%d buffer overflow!\n",i+1);
									memset(tci_instance->SerialInBuffer[i], 0, CAT_PORT_BUFFER_LEN);
									tci_instance->curInBufIndex[i] = 0;
								}
								break;                
						}
					}
				}
			}	
		}
		//delay(1);


	}

}
#endif // !ESP8266

void TCI::send_cat_IF(int rtxId) {
	if (!connected() || !rtx[rtxId].autoInformationEnabled())
		return;
	//loop on every Cat port
	for (int i=0; i<NUM_OF_CAT_PORT;i++) {		
		if (cat_port[i].rtxId == rtxId) {
			sprintf(SerialInBuffer[i],"IF;");
			memset(SerialOutBuffer[i], 0, CAT_PORT_BUFFER_LEN);
			rtx[rtxId].getCatResponse(SerialOutBuffer[i],SerialInBuffer[i]);
			cat_port[i].port->printf("%s",SerialOutBuffer[i]);
		}
	}
}

bool TCI::eval_bool_buf() {
	bool result = false;
	if (strstr(tmp_bool_buffer, "true")) result = true;
	return result;
}

void TCI::send_message() {
	
	if (_connected) {
		//Serial.printf(">>> Sending: >%s<\n",outgoing_message);
 		webSocket.sendTXT(outgoing_message);
	}
}

void TCI::hexdump(const void *mem, uint32_t len, uint8_t cols) {
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
}

void TCI::parse_message(unsigned int length) {

	char *s;

	// Diagnostic tap: print verbatim any incoming TCI message whose
	// payload contains the substring "tune". Helps confirm whether a
	// given server (e.g. AetherSDR) emits tune-related frames under
	// a name the library's parse_message doesn't recognise, without
	// turning on the full TCI_LOG_UNHANDLED flood. Cheap to leave in
	// — fires only on tune-adjacent messages, which are rare.
	if (strstr(incoming_message, "tune") != NULL) {
		Serial.printf("[TCI raw tune-like] %s\r\n", incoming_message);
	}

	/*
	 *  BE CAREFUL! Sorted by event priority
	 */ 


	/*
	 * The following section is only for testing purpose
	 * You can test the TX_SENSORS message without having to tx the rig
	*
	if (strstr(incoming_message, "rx_sensors:")) {

		memset(incoming_message, 0, sizeof(incoming_message));
		sprintf(incoming_message,"tx_sensors:0,-27.2,47.4,67.5,1.7;");
	}

	 *
	 * End TX_SENSORS test code
	 */


	// ===== RX_SENSORS =====
	// ===== rx_sensors:0,-106.4; =====
	                              
	s = strstr(incoming_message, "rx_sensors:");
    if (s != NULL) 	
	{
		int rtxId;
		float dbm_level;
		sscanf(incoming_message, "rx_sensors:%d,%f;", &rtxId, &dbm_level);	
		if (do_rx_sensors_event != NULL)
			do_rx_sensors_event(rtxId,(int)(dbm_level*10));
		return;			
	}

	// ===== VFO =====
	// ===== vfo:0,0,7010600; =====
	s = strstr(incoming_message, "vfo:");
    if (s != NULL) {
		int rtxId, vfoId, freq;
		sscanf(incoming_message, "vfo:%d,%d,%d;", &rtxId, &vfoId, &freq);	
		rtx[rtxId].setVfo(vfoId,freq);
		if (do_vfo_event!=NULL) 
			do_vfo_event(rtxId,vfoId);
		send_cat_IF(rtxId);
		return;			
	}

	// ===== IF =====
	// ===== if:0,0,0; =====
	s = strstr(incoming_message, "if:");
    if (s != NULL) {
		int rtxId, vfoId, freq;
		sscanf(incoming_message, "if:%d,%d,%d;", &rtxId, &vfoId, &freq);		
		rtx[rtxId].setIf(vfoId,freq);
		if (do_if_event!=NULL) 
			do_if_event(rtxId,vfoId);	
		return;			
	}

	// ===== TX_SENSORS =====
	// ===== tx_sensors:0,-27.2,47.4,67.5,1.7; =====	                              
	s = strstr(incoming_message, "tx_sensors:");
    if (s != NULL) 	
	{
		int rtxId;
		float mic_level, rms_power_out, peak_power_out, swr;
		sscanf(incoming_message, "tx_sensors:%d,%f,%f,%f,%f;", 
		       &rtxId, &mic_level, &rms_power_out, &peak_power_out, &swr);	
		if (do_tx_sensors_event != NULL)
			do_tx_sensors_event(rtxId,mic_level,rms_power_out,peak_power_out,swr);
		return;			
	}


	// ===== MODULATION =====
	// ===== modulation:0,CW; =====
	s = strstr(incoming_message, "modulation:");
    if (s != NULL) {
		int rtxId;
		char modulation[10];		
		sscanf(incoming_message, "modulation:%d,%s;", &rtxId, modulation);
		modulation[strlen(modulation)-1] = '\0';
		//Serial.printf(">>> %d, >%s<\n", rtxId, modulation);	
		rtx[rtxId].setModulation(modulation);
		if (do_modulation_event!=NULL) 
			do_modulation_event(rtxId);
		send_cat_IF(rtxId);	
		return;			
	}

	// ===== TRX =====
	// ===== trx:0,false; =====
	s = strstr(incoming_message, "trx:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "trx:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setTrx(eval_bool_buf());
		if (do_trx_event != NULL)
			do_trx_event(rtxId);
		send_cat_IF(rtxId);			
		return;			
	}

	// ===== TX_FREQUENCY =====
	// ===== tx_frequency:7010600; =====
	s = strstr(incoming_message, "tx_frequency:");
    if (s != NULL) {
		sscanf(incoming_message, "tx_frequency:%d;", &tx_frequency);	
		if (do_tx_frequency_event!=NULL) 
			do_tx_frequency_event();
		return;			
	}

	// ===== TUNE =====
	// ===== tune:0,false; =====
	s = strstr(incoming_message, "tune:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "tune:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setTune(eval_bool_buf());
		if (do_tune_event != NULL)
			do_tune_event(rtxId);		
		return;			
	}

	// ===== TUNE_DRIVE =====
	// ===== tune_drive:0,0; =====
	s = strstr(incoming_message, "tune_drive:");
    if (s != NULL) {
		int rtxId, power;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "tune_drive:%d,%d;", &rtxId, &power);
		rtx[rtxId].setTuneDrive(power);
		if (do_tune_drive_event != NULL)
			do_tune_drive_event(rtxId);		
		return;			
	}

	// ===== DRIVE =====
	// ===== drive:0,0; =====
	s = strstr(incoming_message, "drive:");
    if (s != NULL) {
		int rtxId, power;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "drive:%d,%d;", &rtxId, &power);
		rtx[rtxId].setDrive(power);
		if (do_drive_event != NULL)
			do_drive_event(rtxId);		
		return;			
	}

	// ===== TX_ENABLE =====
	// ===== tx_enable:0,true; =====
	s = strstr(incoming_message, "tx_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "tx_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setTxEnable(eval_bool_buf());
		if (do_tx_enable_event != NULL)
			do_tx_enable_event(rtxId);	
		send_cat_IF(rtxId);		
		return;			
	}

	// ===== TX_FOOTSWITCH =====
	// ===== tx_footswitch:0,true; =====	
	s = strstr(incoming_message, "tx_footswitch:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "tx_footswitch:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setTxFootswitch(eval_bool_buf());
		if (do_tx_footswitch_event != NULL)
			do_tx_footswitch_event(rtxId);		
		return;			
	}

	// ===== START =====
	// ===== start; =====	
	s = strstr(incoming_message, "start;");
    if (s != NULL) {
		started = true;
		if (do_started_event != NULL)
			do_started_event();
		return;			
	}
	
	// ===== STOP =====
	// ===== stop; =====	
	s = strstr(incoming_message, "stop;");
    if (s != NULL) {
		started = false;
		if (do_stopped_event != NULL)
			do_stopped_event();
		return;			
	}

	// ===== DDS =====
	// ===== dds:0,7022360; =====
	s = strstr(incoming_message, "dds:");
    if (s != NULL) {
		int rtxId, freq;
		sscanf(incoming_message, "dds:%d,%d;", &rtxId, &freq);
		rtx[rtxId].setDds(freq);
		if (do_dds_event!=NULL) 
			do_dds_event(rtxId);	
		return;			
	}

	// ===== RIT_ENABLE =====
	// ===== rit_enable:0,false; =====
	s = strstr(incoming_message, "rit_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rit_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRitEnable(eval_bool_buf());
		if (do_rit_enable_event!=NULL) 
			do_rit_enable_event(rtxId);	
		send_cat_IF(rtxId);		
		return;			
	}

	// ===== XIT_ENABLE =====
	// ===== xit_enable:0,false; =====
	s = strstr(incoming_message, "xit_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "xit_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setXitEnable(eval_bool_buf());
		if (do_xit_enable_event!=NULL) 
			do_xit_enable_event(rtxId);		
		send_cat_IF(rtxId);	
		return;			
	}


	// ===== SPLIT_ENABLE =====
	// ===== split_enable:0,false; =====
	s = strstr(incoming_message, "split_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "split_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setSplitEnable(eval_bool_buf());
		if (do_split_enable_event!=NULL) 
			do_split_enable_event(rtxId);
		send_cat_IF(rtxId);			
		return;			
	}

	// ===== RIT_OFFSET =====
	// ===== rit_offset:0,0; =====
	s = strstr(incoming_message, "rit_offset:");
    if (s != NULL) {
		int rtxId, freq;
		sscanf(incoming_message, "rit_offset:%d,%d;", &rtxId, &freq);
		rtx[rtxId].setRitOffset(freq);
		if (do_rit_offset_event != NULL)
			do_rit_offset_event(rtxId);		
		send_cat_IF(rtxId);	
		return;			
	}

	// ===== XIT_OFFSET =====
	// ===== xit_offset:0,0; =====
	s = strstr(incoming_message, "xit_offset:");
    if (s != NULL) {
		int rtxId, freq;
		sscanf(incoming_message, "xit_offset:%d,%d;", &rtxId, &freq);
		rtx[rtxId].setXitOffset(freq);
		if (do_xit_offset_event != NULL)
			do_xit_offset_event(rtxId);
		send_cat_IF(rtxId);			
		return;			
	}

	// ===== RX_CHANNEL_ENABLE =====
	// ===== rx_channel_enable:0,1,false; =====
	s = strstr(incoming_message, "rx_channel_enable:");
    if (s != NULL) {
		int rtxId, vfoId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_channel_enable:%d,%d,%s;", &rtxId, &vfoId, tmp_bool_buffer);
		rtx[rtxId].setRxChannelEnable(vfoId,eval_bool_buf());
		if (do_rx_channel_enable_event!=NULL) 
			do_rx_channel_enable_event(rtxId,vfoId);
		return;			
	}

	// ===== RX_FITLER_BAND =====
	// ===== rx_filter_band:0,-300,300; =====
	s = strstr(incoming_message, "rx_filter_band:");
    if (s != NULL) {
		int rtxId, lower, top;
		sscanf(incoming_message, "rx_filter_band:%d,%d,%d;", &rtxId, &lower, &top);
		rtx[rtxId].setRxFilterLower(lower);
		rtx[rtxId].setRxFilterTop(top);	
		if (do_rx_filter_band_event!=NULL) 
			do_rx_filter_band_event(rtxId);
		return;			
	}

	// ===== CW_MACROS_SPEED =====
	// ===== cw_macros_speed:30; =====
	s = strstr(incoming_message, "cw_macros_speed:");
    if (s != NULL) {
		sscanf(incoming_message, "cw_macros_speed:%d;", &cw_macros_speed);	
		if (do_cw_macros_speed_event != NULL)
			do_cw_macros_speed_event();
		return;			
	}

	// ===== CW_MACROS_DELAY =====
	// ===== cw_macros_delay:10; =====
	s = strstr(incoming_message, "cw_macros_delay:");
    if (s != NULL) {
		sscanf(incoming_message, "cw_macros_delay:%d;", &cw_macros_delay);	
		if (do_cw_macros_delay_event != NULL)
			do_cw_macros_delay_event();
		return;			
	}

	// ===== CW_KEYER_SPEED =====
	// ===== cw_keyer_speed:30; =====
	s = strstr(incoming_message, "cw_keyer_speed:");
    if (s != NULL) {
		sscanf(incoming_message, "cw_keyer_speed:%d;", &cw_keyer_speed);	
		if (do_cw_keyer_speed_event != NULL)
			do_cw_keyer_speed_event();
		return;			
	}

	// ===== RX_VOLUME =====
	// ===== rx_volume:0,0,0; =====
	s = strstr(incoming_message, "rx_volume:");
    if (s != NULL) {
		int rtxId, vfoId, db_level;
		sscanf(incoming_message, "rx_volume:%d,%d,%d;", &rtxId, &vfoId, &db_level);	
		rtx[rtxId].setRxVolume(vfoId,db_level);
		if (do_rx_volume_event != NULL)
			do_rx_volume_event(rtxId,vfoId);
		return;			
	}


	// ===== MON_VOLUME =====
	// ===== mon_volume:-30; =====
	s = strstr(incoming_message, "mon_volume:");
    if (s != NULL) {
		sscanf(incoming_message, "mon_volume:%d;", &mon_volume);	
		if (do_mon_volume_event != NULL)
			do_mon_volume_event();
		return;			
	}	

	// ===== VOLUME =====
	// ===== volume:-30; =====
	s = strstr(incoming_message, "volume:");
    if (s != NULL) {
		sscanf(incoming_message, "volume:%d;", &volume);	
		if (do_volume_event != NULL)
			do_volume_event();
		return;			
	}

	// ===== RX_MUTE =====
	// ===== rx_mute:0,false; =====
	s = strstr(incoming_message, "rx_mute:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_mute:%d,%s;",&rtxId,tmp_bool_buffer);
		rtx[rtxId].setRxMute(eval_bool_buf());
		if (do_rx_mute_event != NULL)
			do_rx_mute_event(rtxId);
		return;			
	}

	// ===== MUTE =====
	// ===== mute:false; =====
	s = strstr(incoming_message, "mute:");
    if (s != NULL) {
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "mute:%s;", tmp_bool_buffer);
		mute = eval_bool_buf() ?  true : false;
		if (do_mute_event != NULL)
			do_mute_event();
		return;			
	}

	// ===== RX_BALANCE =====
	// ===== rx_balance:0,0,0; =====
	s = strstr(incoming_message, "rx_balance:");
    if (s != NULL) {
		int rtxId, vfoId, db_level;
		sscanf(incoming_message, "rx_balance:%d,%d,%d;", &rtxId, &vfoId, &db_level);	
		rtx[rtxId].setRxBalance(vfoId,db_level);
		if (do_rx_balance_event != NULL)
			do_rx_balance_event(rtxId,vfoId);
		return;			
	}

	// ===== MON_ENABLE =====
	// ===== mon_enable:false; =====
	s = strstr(incoming_message, "mon_enable:");
    if (s != NULL) {
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "mon_enable:%s;", tmp_bool_buffer);
		mon_enable = eval_bool_buf() ?  true : false;
		if (do_mon_enable_event != NULL)
			do_mon_enable_event();
		return;			
	}

	// ===== AGC_MODE =====
	// ===== agc_mode:0,false; =====
	s = strstr(incoming_message, "agc_mode:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "agc_mode:%d,%s;", &rtxId, tmp_bool_buffer);		
		if (strstr(tmp_bool_buffer, "off;") != NULL) {
			rtx[rtxId].setAgcMode(0);	
		} else if (strstr(tmp_bool_buffer, "fast;") != NULL) {
			rtx[rtxId].setAgcMode(1);	
		} else if (strstr(tmp_bool_buffer, "normal;") != NULL) {
			rtx[rtxId].setAgcMode(2);
		}

		if (do_agc_mode_event != NULL)
			do_agc_mode_event(rtxId);		
		return;			
	}

	// ===== AGC_GAIN =====
	// ===== agc_gain:0,0; =====
	s = strstr(incoming_message, "agc_gain:");
    if (s != NULL) {
		int rtxId, db_level;
		sscanf(incoming_message, "agc_gain:%d,%d;", &rtxId, &db_level);	
		rtx[rtxId].setAgcGain(db_level);
		if (do_agc_gain_event != NULL)
			do_agc_gain_event(rtxId);
		return;			
	}

	// ===== RX_NB_ENABLE =====
	// ===== rx_nb_enable:0,true; =====
	s = strstr(incoming_message, "rx_nb_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_nb_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxNbEnable(eval_bool_buf());
		if (do_rx_nb_enable_event != NULL)
			do_rx_nb_enable_event(rtxId);		
		return;			
	}

	// ===== RX_NB_PARAM =====
	// ===== rx_nb_param:0,0,0; =====
	s = strstr(incoming_message, "rx_nb_param:");
    if (s != NULL) {
		int rtxId, trig_thres, pulse_dur;
		sscanf(incoming_message, "rx_nb_param:%d,%d,%d;", &rtxId, &trig_thres, &pulse_dur);	
		rtx[rtxId].setTriggeringThresold(trig_thres);
		rtx[rtxId].setPulseDuration(pulse_dur);		
		if (do_rx_nb_param_event != NULL)
			do_rx_nb_param_event(rtxId);
		return;			
	}

	// ===== RX_BIN_ENABLE =====
	// ===== rx_bin_enable:0,true; =====
	s = strstr(incoming_message, "rx_bin_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_bin_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxBinEnable(eval_bool_buf());
		if (do_rx_bin_enable_event != NULL)
			do_rx_bin_enable_event(rtxId);		
		return;			
	}


	// ===== RX_NR_ENABLE =====
	// ===== rx_nr_enable:0,true; =====
	s = strstr(incoming_message, "rx_nr_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_nr_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxNrEnable(eval_bool_buf());
		if (do_rx_nr_enable_event != NULL)
			do_rx_nr_enable_event(rtxId);		
		return;			
	}

	// ===== RX_ANC_ENABLE =====
	// ===== rx_anc_enable:0,true; =====
	s = strstr(incoming_message, "rx_anc_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_anc_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxAncEnable(eval_bool_buf());
		if (do_rx_anc_enable_event != NULL)
			do_rx_anc_enable_event(rtxId);		
		return;			
	}

	// ===== RX_ANF_ENABLE =====
	// ===== rx_anf_enable:0,true; =====
	s = strstr(incoming_message, "rx_anf_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_anf_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxAnfEnable(eval_bool_buf());
		if (do_rx_anf_enable_event != NULL)
			do_rx_anf_enable_event(rtxId);		
		return;			
	}

	// ===== RX_APF_ENABLE =====
	// ===== rx_apf_enable:0,true; =====
	s = strstr(incoming_message, "rx_apf_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_apf_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxApfEnable(eval_bool_buf());
		if (do_rx_apf_enable_event != NULL)
			do_rx_apf_enable_event(rtxId);		
		return;			
	}

	// ===== RX_DSE_ENABLE =====
	// ===== rx_dse_enable:0,true; =====
	s = strstr(incoming_message, "rx_dse_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_dse_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxDseEnable(eval_bool_buf());
		if (do_rx_dse_enable_event != NULL)
			do_rx_dse_enable_event(rtxId);		
		return;			
	}

	// ===== RX_NF_ENABLE =====
	// ===== rx_nf_enable:0,true; =====
	s = strstr(incoming_message, "rx_nf_enable:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "rx_nf_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setRxNfEnable(eval_bool_buf());
		if (do_rx_nf_enable_event != NULL)
			do_rx_nf_enable_event(rtxId);		
		return;			
	}

	// ===== LOCK =====
	// ===== lock:0,true; =====
	s = strstr(incoming_message, "lock:");
    if (s != NULL) {
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "lock:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setLock(eval_bool_buf());
		if (do_lock_event != NULL)
			do_lock_event(rtxId);		
		return;			
	}

	// ===== SQL_ENABLE =====
	// ===== sql_enable:0,true; =====
	s = strstr(incoming_message, "sql_enable:");
    if (s != NULL) {		
		int rtxId;
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "sql_enable:%d,%s;", &rtxId, tmp_bool_buffer);
		rtx[rtxId].setSqlEnable(eval_bool_buf());
		if (do_sql_enable_event != NULL)
			do_sql_enable_event(rtxId);		
		return;			
	}

	// ===== SQL_LEVEL =====
	// ===== sql_level:0,0; =====
	s = strstr(incoming_message, "sql_level:");
    if (s != NULL) {
		int rtxId, db_level;
		sscanf(incoming_message, "sql_level:%d,%d;", &rtxId, &db_level);	
		rtx[rtxId].setSqlLevel(db_level);
		if (do_sql_level_event != NULL)
			do_sql_level_event(rtxId);
		return;			
	}

	// ===== IQ_SAMPLERATE =====
	// ===== iq_samplerate:48000; =====
	s = strstr(incoming_message, "iq_samplerate:");
    if (s != NULL) {
		sscanf(incoming_message, "iq_samplerate:%d;", &iq_samplerate);	
		if (do_iq_samplerate_event != NULL)
			do_iq_samplerate_event();
		return;			
	}

	// ===== AUDIO_SAMPLERATE =====
	// ===== audio_samplerate:24000; =====
	s = strstr(incoming_message, "audio_samplerate:");
    if (s != NULL) {
		sscanf(incoming_message, "audio_samplerate:%d;", &audio_samplerate);	
		if (do_audio_samplerate_event != NULL)
			do_audio_samplerate_event();
		return;			
	}

	// ===== IQ_START =====
	// ===== iq_start:0; =====
	s = strstr(incoming_message, "iq_start:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "iq_start:%d;", &rtxId);
		rtx[rtxId].iqStart();	
		if (do_iq_start_stop_event != NULL)
			do_iq_start_stop_event(rtxId);
		return;			
	}

	// ===== IQ_STOP =====
	// ===== iq_stop:0; =====
	s = strstr(incoming_message, "iq_stop:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "iq_stop:%d;", &rtxId);
		rtx[rtxId].iqStop();	
		if (do_iq_start_stop_event != NULL)
			do_iq_start_stop_event(rtxId);
		return;			
	}

	// ===== AUDIO_START =====
	// ===== audio_start:0; =====
	s = strstr(incoming_message, "audio_start:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "audio_start:%d;", &rtxId);
		rtx[rtxId].audioStart();	
		if (do_audio_start_stop_event != NULL)
			do_audio_start_stop_event(rtxId);
		return;			
	}

	// ===== AUDIO_STOP =====
	// ===== audio_stop:0; =====
	s = strstr(incoming_message, "audio_stop:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "audio_stop:%d;", &rtxId);
		rtx[rtxId].audioStop();	
		if (do_audio_start_stop_event != NULL)
			do_audio_start_stop_event(rtxId);
		return;			
	}

	// ===== LINE_OUT_START =====
	// ===== line_out_start:0; =====
	s = strstr(incoming_message, "line_out_start:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "line_out_start:%d;", &rtxId);
		rtx[rtxId].lineOutStart();
		if (do_line_out_start_stop_event != NULL)
			do_line_out_start_stop_event(rtxId);
		return;			
	}

	// ===== LINE_OUT_STOP =====
	// ===== line_out_stop:0; =====
	s = strstr(incoming_message, "line_out_stop:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "line_out_stop:%d;", &rtxId);
		rtx[rtxId].lineOutStop();	
		if (do_line_out_start_stop_event != NULL)
			do_line_out_start_stop_event(rtxId);
		return;			
	}

	// ===== LINE_OUT_RECORDER_START =====
	// ===== line_out_recorder_start:0; =====
	s = strstr(incoming_message, "line_out_recorder_start:");
    if (s != NULL) {
		int rtxId, record_duration;
		sscanf(incoming_message, "line_out_recorder_start:%d,%d;", &rtxId, &record_duration);
		rtx[rtxId].setRecordDuration(record_duration);
		if (do_line_out_recorder_start_event != NULL)
			do_line_out_recorder_start_event(rtxId);
		return;			
	}

	// ===== LINE_OUT_RECORDER_SAVE =====
	// ===== line_out_recorder_save:0; =====
	s = strstr(incoming_message, "line_out_recorder_save:");
    if (s != NULL) {
		int rtxId;
		char f_name[128];
		sscanf(incoming_message, "line_out_recorder_save:%d,%s;", &rtxId,f_name);
		rtx[rtxId].setRecordFileName(f_name);	
		if (do_line_out_recorder_save_event != NULL)
			do_line_out_recorder_save_event(rtxId);
		return;			
	}


	// ===== LINE_OUT_RECORDER_BREAK =====
	// ===== line_out_recorder_start:0; =====
	s = strstr(incoming_message, "line_out_recorder_break:");
    if (s != NULL) {
		int rtxId;
		sscanf(incoming_message, "line_out_recorder_break:%d;", &rtxId);		
		if (do_line_out_recorder_break_event != NULL)
			do_line_out_recorder_break_event(rtxId);
		return;			
	}
	
	// ===== RX_CLICKED_ON_SPOT =====
	// ===== rx_clicked_on_spot:IW7DMH,28460000; =====
	s = strstr(incoming_message, "rx_clicked_on_spot:");
    if (s != NULL) {
		memset(clicked_spot.callsing, 0, sizeof(clicked_spot.callsing));
		for(int i=0;incoming_message[i];i++) {  
			if (incoming_message[i]==',') incoming_message[i]=' ';
		}
		sscanf(incoming_message, "rx_clicked_on_spot:%d %d %s %d;", 
		       &clicked_spot.rtxId,
			   &clicked_spot.vfoId,
			   clicked_spot.callsing,
			   &clicked_spot.frequency);
		if (do_rx_clicked_on_spot_event != NULL)
			do_rx_clicked_on_spot_event();
		return;			
	}

	// ===== CLICKED_ON_SPOT =====
	// ===== clicked_on_spot:IW7DMH,28460000; =====
	s = strstr(incoming_message, "clicked_on_spot:");
    if (s != NULL) {
		memset(clicked_spot.callsing, 0, sizeof(clicked_spot.callsing));
		for(int i=0;incoming_message[i];i++) {  
			if (incoming_message[i]==',') incoming_message[i]=' ';
		}
		sscanf(incoming_message, "clicked_on_spot:%s %u;", 
			   clicked_spot.callsing,
			   &clicked_spot.frequency);
    	clicked_spot.rtxId = -1;
		clicked_spot.vfoId = -1;					   
		if (do_clicked_on_spot_event != NULL)
			do_clicked_on_spot_event();
		return;			
	}

	// ===== APP_FOCUS =====
	// ===== app_focus:false; =====
	s = strstr(incoming_message, "app_focus:");
    if (s != NULL) {
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "app_focus:%s;", tmp_bool_buffer);
		app_focus = eval_bool_buf() ?  true : false;
		if (do_app_focus_event != NULL)
			do_app_focus_event();
		return;			
	}

		// ===== VFO_LIMITS =====
	// ===== vfo_limits:0,160000000 =====
	s = strstr(incoming_message, "vfo_limits:");
    if (s != NULL) {
		//sscanf(incoming_message, "vfo_limits:%d,%d;", &vfo_limits[0], &vfo_limits[1]);
		float low, high;
		sscanf(incoming_message, "vfo_limits:%f,%f;", &low, &high);
		vfo_limits[0] = low;
		vfo_limits[1] = high;		
		if (do_vfo_limits_event != NULL)
			do_vfo_limits_event(vfo_limits[0],vfo_limits[1]);
		return;			
	}

	// ===== IF_LIMITS =====
	// ===== if_limits:-19531,19531; =====
	s = strstr(incoming_message, "if_limits:");
    if (s != NULL) {
		sscanf(incoming_message, "if_limits:%d,%d;", &if_limits[0], &if_limits[1]);	
		if (do_if_limits_event != NULL)
			do_if_limits_event(if_limits[0],if_limits[1]);
		return;			
	}

	// ===== TRX_COUNT =====
	// ===== trx_count:2; =====
	s = strstr(incoming_message, "trx_count:");
    if (s != NULL) {
		sscanf(incoming_message, "trx_count:%d;", &trx_count);	
		if (do_trx_count_event != NULL)
			do_trx_count_event(trx_count);
		return;			
	}

	// ===== CHANNELS_COUNT =====
	// ===== channels_count:2; =====
	s = strstr(incoming_message, "channels_count:");
    if (s != NULL) {
		sscanf(incoming_message, "channels_count:%d;", &channels_count);	
		if (do_channels_count_event != NULL)
			do_channels_count_event(channels_count);
		return;			
	}

	// ===== DEVICE =====
	// ===== device:SunSDR2DX; =====
	s = strstr(incoming_message, "device:");
    if (s != NULL) {
		memset(device, 0, sizeof(device));
		sscanf(incoming_message, "device:%s;", device);
		device[length-8] = '\0';		
		if (do_device_event != NULL)
			do_device_event();		
		return;			
	}

	// ===== RECEIVE_ONLY =====
	// ===== receive_only:false; =====
	s = strstr(incoming_message, "receive_only:");
    if (s != NULL) {
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "receive_only:%s;", tmp_bool_buffer);
		receive_only = eval_bool_buf() ?  true : false;

		if (do_receive_only_event != NULL)
			do_receive_only_event();
		return;			
	}

	// ===== MODULATIONS_LIST =====
	// ===== modulations_list:am,sam,dsb,lsb,usb,cw,nfm,digl,digu,wfm,drm; =====
	s = strstr(incoming_message, "modulations_list:");
    if (s != NULL) {
		memset(modulations_list, 0, sizeof(modulations_list));
		sscanf(incoming_message, "modulations_list:%s;", modulations_list);
		modulations_list[length-18] = '\0';	
		if (do_modulations_list_event != NULL)
			do_modulations_list_event();		
		return;			
	}

	// ===== PROTOCOL =====
	// ===== protocol:ExpertSDR3,1.7; =====
	s = strstr(incoming_message, "protocol:");
    if (s != NULL) {
		memset(protocol, 0, sizeof(protocol));
		sscanf(incoming_message, "protocol:%s;", protocol);

		//device[length-8] = '\0';		
		if (do_protocol_event != NULL)
			do_protocol_event();		
		return;			
	}

	// ===== READY =====
	// ===== ready; =====
	s = strstr(incoming_message, "ready;");
    if (s != NULL) {
		ready = true;
		if (do_ready_event != NULL)
			do_ready_event();
		return;			
	}
	
	// ===== AUDIO_STREAM_SAMPLE_TYPE =====
	// ===== audio_stream_sample_type:float32; =====
	s = strstr(incoming_message, "audio_stream_sample_type:");
    if (s != NULL) {
		memset(tmp_bool_buffer, 0, sizeof(tmp_bool_buffer));
		sscanf(incoming_message, "audio_stream_sample_rate:%s;", tmp_bool_buffer);
		
		audio_stream_sample_type = 0;
		s = strstr(incoming_message, "int16");
		if (s != NULL) audio_stream_sample_type = 1;
		s = strstr(incoming_message, "int24");
		if (s != NULL) audio_stream_sample_type = 2;
		s = strstr(incoming_message, "int32");
		if (s != NULL) audio_stream_sample_type = 3;
		s = strstr(incoming_message, "float32");
		if (s != NULL) audio_stream_sample_type = 4;
			
		if (do_audio_stream_sample_type_event != NULL)
			do_audio_stream_sample_type_event(audio_stream_sample_type);
		return;			
	}
	
	// ===== AUDIO_STREAM_CHANNELS =====
	// ===== audio_stream_channels:2; =====
	s = strstr(incoming_message, "audio_stream_channels:");
    if (s != NULL) {
		sscanf(incoming_message, "audio_stream_channels:%d;", &audio_stream_channels_count);	
		if (do_audio_stream_channels_event != NULL)
			do_audio_stream_channels_event(audio_stream_channels_count);
		return;			
	}

	// ===== AUDIO_STREAM_SAMPLES =====
	// ===== audio_stream_samples:512; =====
	s = strstr(incoming_message, "audio_stream_samples:");
    if (s != NULL) {
		sscanf(incoming_message, "audio_stream_samples:%d;", &audio_stream_samples);	
		if (do_audio_stream_samples_event != NULL)
			do_audio_stream_samples_event(audio_stream_samples);
		return;			
	}

	// ===== DIGL_OFFSET =====
	// ===== digl_offset:100; =====
	s = strstr(incoming_message, "digl_offset:");
    if (s != NULL) {
		sscanf(incoming_message, "digl_offset:%d;", &digl_offset);	
		if (do_digl_offset_event != NULL)
			do_digl_offset_event(digl_offset);
		return;			
	}

	// ===== DIGU_OFFSET =====
	// ===== digu_offset:100; =====
	s = strstr(incoming_message, "digu_offset:");
    if (s != NULL) {
		sscanf(incoming_message, "digu_offset:%d;", &digu_offset);	
		if (do_digu_offset_event != NULL)
			do_digu_offset_event(digu_offset);
		return;			
	}

#if TCI_LOG_UNHANDLED
	Serial.printf("%03u - %s - Unhandled message!\n",ws_messages[read_index].len,incoming_message);
#endif

}

//
// ********************** INITIALIZATIONS COMMANDS ***********************
//

// ===== VFO_LIMITS =====
void TCI::attach_vfo_limits_event(eventHandlerRigVfoFunction _eventHandler) {
	do_vfo_limits_event=_eventHandler;
}

int TCI::get_vfo_low_limit()
{
	return vfo_limits[0];
}

int TCI::get_vfo_high_limit()
{
	return vfo_limits[1];
}

// ===== IF_LIMITS =====
void TCI::attach_if_limits_event(eventHandlerRigVfoFunction _eventHandler) {
	do_if_limits_event=_eventHandler;
}

int TCI::get_if_low_limit()
{
	return if_limits[0];
}

int TCI::get_if_high_limit()
{
	return if_limits[1];
}

// ===== TRX_COUNT =====
void TCI::attach_trx_count_event(eventHandlerRigFunction _eventHandler) {
	do_trx_count_event = _eventHandler;
}

int TCI::get_trx_count() {
	return trx_count;
}

// ===== CHANNELS_COUNT =====
void TCI::attach_channels_count_event(eventHandlerRigFunction _eventHandler) {
	do_channels_count_event = _eventHandler;
}

int TCI::get_channels_count() {
	return trx_count;
}

// ===== DEVICE =====
void TCI::attach_device_event(eventHandlerFunction _eventHandler) {
	do_device_event = _eventHandler;
}

void TCI::get_device(char *result_buffer, int *len, int buf_size) {	
	
	(*len) = strlen(device);

	if (buf_size >= strlen(device)) {
		strncpy(result_buffer, device, sizeof(device));			
	} else {
		(*len) = 0;
		Serial.printf("Unable to set Device Name - Buffer length is %d while actual device name is %d ",
		              buf_size, strlen(device));
	}
			
}

// ===== RECEIVE_ONLY =====
void TCI::attach_receive_only_event(eventHandlerFunction _eventHandler) {
	do_receive_only_event=_eventHandler;
}

bool TCI::is_receive_only() {
	return receive_only;
}

// ===== MODULATIONS_LIST =====
void TCI::attach_modulations_list_event(eventHandlerFunction _eventHandler) {
	do_modulations_list_event=_eventHandler;
}

void TCI::get_modulations_list(char *result_buffer, int *len, int buf_size) {	
	
	(*len) = strlen(modulations_list);

	if (buf_size >= strlen(modulations_list)) {
		strncpy(result_buffer, modulations_list, sizeof(modulations_list));			
	} else {
		(*len) = 0;
		Serial.printf("Unable to set Modulations List - Buffer length is %d while actual device name is %d ",
		              buf_size, strlen(modulations_list));
	}
			
}

// ===== PROTOCOL =====
void TCI::attach_protocol_event(eventHandlerFunction _eventHandler) {
	do_protocol_event=_eventHandler;
}

void TCI::get_protocol(char *result_buffer, int *len, int buf_size) {	
	
	(*len) = strlen(protocol);

	if (buf_size >= strlen(protocol)) {
		strncpy(result_buffer, protocol, sizeof(protocol));			
	} else {
		(*len) = 0;
		Serial.printf("Unable to set Protocol Name - Buffer length is %d while actual device name is %d ",
		              buf_size, strlen(protocol));
	}
			
}

// ===== READY =====
void TCI::attach_ready_event(eventHandlerFunction _eventHandler) {
	do_ready_event=_eventHandler;
}

bool TCI::is_ready() {
	return ready;
}

//
// ********************** BIDIRECTIONAL CONTROL COMMANDS ***********************
//

void TCI::set_tx_footswitch(int rtxId, bool value) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		if (value)
			sprintf(outgoing_message,"tx_footswitch:%d,%s;",rtxId,"true");
		else
			sprintf(outgoing_message,"tx_footswitch:%d,%s;",rtxId,"false");
	}
	send_message();			  
}

// ===== START =====
void TCI::attach_started_event(eventHandlerFunction _eventHandler) {
	do_started_event=_eventHandler;
}

bool TCI::is_started() {
	return started;
}

void TCI::start_device() {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"start;");
	send_message();
}


// ===== STOP =====
void TCI::attach_stopped_event(eventHandlerFunction _eventHandler) {
	do_stopped_event=_eventHandler;
}

void TCI::stop_device() {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"stop;");
	send_message();
}

// ===== DDS =====
void TCI::attach_dds_event(eventHandlerRigFunction _eventHandler) {
	do_dds_event=_eventHandler;
}

void TCI::set_dds(int rtxId, int freq) {
	if (rtxId>=0 && rtxId<N_MAX_RTX &&
		freq>=get_vfo_low_limit() && freq <=get_vfo_high_limit()) 
	{
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"dds:%d,%d;",rtxId,freq);	
		send_message();	
	}						  
}

// ===== IF =====
void TCI::attach_if_event(eventHandlerRigVfoFunction _eventHandler) {
	do_if_event=_eventHandler;
}

void TCI::set_if(int rtxId, int vfoId, int freq) {
	if (rtxId>=0 && rtxId<N_MAX_RTX &&
	    vfoId>=0 && vfoId<N_MAX_CHANNEL &&
		freq>=get_vfo_low_limit() && freq <=get_vfo_high_limit()) 
	{
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"if:%d,%d,%d;",rtxId,vfoId,freq);	
		send_message();	
	}						  
}

// ===== VFO =====
void TCI::attach_vfo_event(eventHandlerRigVfoFunction _eventHandler) {
	do_vfo_event=_eventHandler;
}

void TCI::set_vfo(int rtxId, int vfoId, int freq) {

	//Serial.printf("%d,%d,%d,%d,%d,%d,%d\n",rtxId, vfoId, freq,
	//              N_MAX_RTX,N_MAX_CHANNEL,get_vfo_low_limit(),get_vfo_high_limit());

	if (rtxId>=0 && rtxId<N_MAX_RTX &&
	    vfoId>=0 && vfoId<N_MAX_CHANNEL &&
		freq>=get_vfo_low_limit() && freq <=get_vfo_high_limit())
	{	 
		//vfo:1,0,14175000;
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"vfo:%d,%d,%d;",rtxId,vfoId,freq);			
		send_message();							  
	}
}

// ===== MODULATION =====
void TCI::attach_modulation_event(eventHandlerRigFunction _eventHandler) {
	do_modulation_event=_eventHandler;
}

void TCI::set_modulation(int rtxId, char *modulation) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"modulation:%d,%s;",rtxId,modulation);			
		send_message();							  
	}						  
}

// ===== TRX =====
void TCI::attach_trx_event(eventHandlerRigFunction _eventHandler) {
	do_trx_event=_eventHandler;
}

void TCI::set_trx(int rtxId, trx_mode mode) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		switch (mode) {
			case TCI::TRX_CHECK:
				sprintf(outgoing_message,"trx:%d;",rtxId);			
				break;	
			case TCI::TRX_TRUE:
				sprintf(outgoing_message,"trx:%d,%s;",rtxId,"true");			
				break;	
			case TCI::TRX_FALSE:
				sprintf(outgoing_message,"trx:%d,%s;",rtxId,"false");			
				break;	
			case TCI::TRX_TRUE_TCI:
				sprintf(outgoing_message,"trx:%d,%s,tci;",rtxId,"true");			
				break;	
			case TCI::TRX_FALSE_TCI:
				sprintf(outgoing_message,"trx:%d,%s,tci;",rtxId,"false");			
				break;			
			default:
				Serial.printf("TRX command error - Invalid mode!\n");
		}
		send_message();
	}
}

// ===== TUNE =====
void TCI::attach_tune_event(eventHandlerRigFunction _eventHandler) {
	do_tune_event=_eventHandler;
}

void TCI::set_tune(int rtxId, tune_mode mode) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		switch (mode) {
			case TCI::TUNE_CHECK:
				sprintf(outgoing_message,"tune:%d;",rtxId);			
				break;	
			case TCI::TUNE_TRUE:
				sprintf(outgoing_message,"tune:%d,%s;",rtxId,"true");			
				break;	
			case TCI::TUNE_FALSE:
				sprintf(outgoing_message,"tune:%d,%s;",rtxId,"false");			
				break;			
			default:
				Serial.printf("TUNE command error - Invalid mode!\n");
		}
		send_message();
	}
}

// ===== DRIVE =====
void TCI::attach_drive_event(eventHandlerRigFunction _eventHandler) {
	do_drive_event=_eventHandler;
}

void TCI::set_drive(int rtxId, int power) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"drive:%d,%d;",rtxId,power);			
		send_message();		
	}
}

// ===== TUNE_DRIVE =====
void TCI::attach_tune_drive_event(eventHandlerRigFunction _eventHandler) {
	do_tune_drive_event=_eventHandler;
}

void TCI::set_tune_drive(int rtxId, int power) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"tune_drive:%d,%d;",rtxId,power);			
		send_message();		
	}
}

// ===== RIT_ENABLE =====
void TCI::attach_rit_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rit_enable_event=_eventHandler;
}

void TCI::set_rit_enable(int rtxId, bool value) {
	if (rtxId>=0 && rtxId<=N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		if (value)
			sprintf(outgoing_message,"rit_enable:%d,%s;",rtxId,"true");
		else
			sprintf(outgoing_message,"rit_enable:%d,%s;",rtxId,"false");
	}
	send_message();			  
}

// ===== XIT_ENABLE =====
void TCI::attach_xit_enable_event(eventHandlerRigFunction _eventHandler) {
	do_xit_enable_event=_eventHandler;
}

void TCI::set_xit_enable(int rtxId, bool value) {
	if (rtxId>=0 && rtxId<=N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		if (value)
			sprintf(outgoing_message,"xit_enable:%d,%s;",rtxId,"true");
		else
			sprintf(outgoing_message,"xit_enable:%d,%s;",rtxId,"false");
	}
	send_message();			  
}

// ===== SPLIT_ENABLE =====
void TCI::attach_split_enable_event(eventHandlerRigFunction _eventHandler) {
	do_split_enable_event=_eventHandler;
}

void TCI::set_split_enable(int rtxId, bool value) {
	if (rtxId>=0 && rtxId<=N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		if (value)
			sprintf(outgoing_message,"split_enable:%d,%s;",rtxId,"true");
		else
			sprintf(outgoing_message,"split_enable:%d,%s;",rtxId,"false");
	}
	send_message();			  
}

// ===== RIT_OFFSET =====
void TCI::attach_rit_offset_event(eventHandlerRigFunction _eventHandler) {
	do_rit_offset_event=_eventHandler;
}

void TCI::set_rit_offset(int rtxId, int freq) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"rit_offset:%d,%d;",rtxId,freq);			
		send_message();		
	}
}

// ===== XIT_OFFSET =====
void TCI::attach_xit_offset_event(eventHandlerRigFunction _eventHandler) {
	do_xit_offset_event=_eventHandler;
}

void TCI::set_xit_offset(int rtxId, int freq) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"xit_offset:%d,%d;",rtxId,freq);			
		send_message();		
	}
}

// ===== RX_CHANNEL_ENABLE =====
void TCI::attach_rx_channel_enable_event(eventHandlerRigVfoFunction _eventHandler) {
	do_rx_channel_enable_event=_eventHandler;
}

void TCI::set_rx_channel_enable(int rtxId, int vfoId, bool enable) {
	if (rtxId>=0 && rtxId<N_MAX_RTX &&
	    vfoId>=0 && vfoId<N_MAX_CHANNEL)
	{	 
		memset(outgoing_message, 0, sizeof(outgoing_message));
		if (enable)
			sprintf(outgoing_message,"vfo:%d,%d,true;",rtxId,vfoId);	
		else
			sprintf(outgoing_message,"vfo:%d,%d,false;",rtxId,vfoId);			
		send_message();							  
	}
}

// ===== RX_FILTER_BAND =====
void TCI::attach_rx_filter_band_event(eventHandlerRigFunction _eventHandler) {
	do_rx_filter_band_event=_eventHandler;
}

void TCI::set_rx_filter_band(int rtxId, int lower, int top) {
	if (rtxId>=0 && rtxId<N_MAX_RTX)
	{	 
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"rx_filter_band:%d,%d,%d;",rtxId,lower,top);
		send_message();							  
	}
}

// ===== CW_MACROS_SPEED =====
void TCI::attach_cw_macros_speed_event(eventHandlerFunction _eventHandler) {
	do_cw_macros_speed_event=_eventHandler;
}

void TCI::set_cw_macros_speed(int speed) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"cw_macros_speed:%d;",speed);
	send_message();	
}

int TCI::get_cw_macros_speed() {
	return cw_macros_speed;
}

// ===== CW_MACROS_DELAY =====
void TCI::attach_cw_macros_delay_event(eventHandlerFunction _eventHandler) {
	do_cw_macros_delay_event=_eventHandler;
}

void TCI::set_cw_macros_delay(int milliseconds) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"cw_macros_delay:%d;",milliseconds);
	send_message();	
}

int TCI::get_cw_macros_delay() {
	return cw_macros_delay;
}

// ===== CW_KEYER_SPEED =====
void TCI::attach_cw_keyer_speed_event(eventHandlerFunction _eventHandler) {
	do_cw_keyer_speed_event=_eventHandler;
}

void TCI::set_cw_keyer_speed(int speed) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"cw_keyer_speed:%d;",speed);
	send_message();	
}

int TCI::get_cw_keyer_speed() {
	return cw_keyer_speed;
}

// ===== VOLUME =====
void TCI::attach_volume_event(eventHandlerFunction _eventHandler) {
	do_volume_event=_eventHandler;
}

void TCI::set_volume(int db_level) {	
	if (db_level >= -60 && db_level <=0) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"volume:%d;",db_level);
		send_message();	
	}
	
}

int TCI::get_volume() {
	return volume;
}

// ===== MUTE =====
void TCI::attach_mute_event(eventHandlerFunction _eventHandler) {
	do_mute_event=_eventHandler;
}

void TCI::set_mute(bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"mute:true;");
	else
		sprintf(outgoing_message,"mute:false;");
	send_message();			  
}

bool TCI::is_mute() {
	return mute;
}

// ===== RX_MUTE =====
// ===== rx_mute:0,false; =====
void TCI::attach_rx_mute_event(eventHandlerRigFunction _eventHandler) {
	do_rx_mute_event=_eventHandler;
}

void TCI::set_rx_mute(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_mute:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_mute:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_VOLUME =====
void TCI::attach_rx_volume_event(eventHandlerRigVfoFunction _eventHandler) {
	do_rx_volume_event=_eventHandler;
}

void TCI::set_rx_volume(int rtxId, int vfoId, int db_level) {	

	if (rtxId>=0 && rtxId<N_MAX_RTX &&
	    vfoId>=0 && vfoId<N_MAX_CHANNEL &&
		db_level >= -60 && db_level <=0) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"rx_volume:%d,%d,%d;",rtxId,vfoId,db_level);
		send_message();	
	}
	
}

// ===== RX_BALANCE =====
void TCI::attach_rx_balance_event(eventHandlerRigVfoFunction _eventHandler) {
	do_rx_balance_event=_eventHandler;
}

void TCI::set_rx_balance(int rtxId, int vfoId, int db_level) {	

	if (rtxId>=0 && rtxId<N_MAX_RTX &&
	    vfoId>=0 && vfoId<N_MAX_CHANNEL &&
		db_level >= -40 && db_level <=40) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"rx_balance:%d,%d,%d;",rtxId,vfoId,db_level);
		send_message();	
	}
	
}

// ===== MON_VOLUME =====
void TCI::attach_mon_volume_event(eventHandlerFunction _eventHandler) {
	do_mon_volume_event=_eventHandler;
}

void TCI::set_mon_volume(int db_level) {	
	if (db_level >= -60 && db_level <=0) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"mon_volume:%d;",volume);
		send_message();	
	}
	
}

int TCI::get_mon_volume() {
	return mon_volume;
}

// ===== MON_ENABLE =====
void TCI::attach_mon_enable_event(eventHandlerFunction _eventHandler) {
	do_mon_enable_event=_eventHandler;
}

void TCI::set_mon_enable(bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"mon_enable:true;");
	else
		sprintf(outgoing_message,"mon_enable:false;");
	send_message();			  
}

bool TCI::is_mon_enable() {
	return mon_enable;
}

// ===== AGC_MODE =====
void TCI::attach_agc_mode_event(eventHandlerRigFunction _eventHandler) {
	do_agc_mode_event=_eventHandler;
}

void TCI::set_agc_mode(int rtxId, int mode) {
	if (rtxId>=0 && rtxId<N_MAX_RTX) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		switch (mode) {
			case 0:
				sprintf(outgoing_message,"agc_mode:%d,off;",rtxId);			
				break;	
			case 1:
				sprintf(outgoing_message,"agc_mode:%d,fast;",rtxId);			
				break;	
			case 2:
				sprintf(outgoing_message,"agc_mode:%d,normal;",rtxId);
				break;			
			default:
				Serial.printf("AGC MODE command error - Invalid mode!\n");
		}
		send_message();
	}
}

// ===== AGC_GAIN =====
void TCI::attach_agc_gain_event(eventHandlerRigFunction _eventHandler) {
	do_agc_gain_event=_eventHandler;
}

void TCI::set_agc_gain(int rtxId, int db_level) {	

	if (rtxId>=0 && rtxId<N_MAX_RTX && 
		db_level >= -20 && db_level <=120) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"agc_gain:%d,%d;",rtxId,db_level);
		send_message();	
	}
	
}

// ===== RX_NB_ENABLE =====
void TCI::attach_rx_nb_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_nb_enable_event=_eventHandler;
}

void TCI::set_rx_nb_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_nb_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_nb_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_NB_PARAM =====
void TCI::attach_rx_nb_param_event(eventHandlerRigFunction _eventHandler) {
	do_rx_nb_param_event=_eventHandler;
}

void TCI::set_rx_nb_param(int rtxId, int triggering_threshold, int pulse_duration) {	

	if (rtxId>=0 && rtxId<N_MAX_RTX &&
		triggering_threshold >= 1 && triggering_threshold <= 100 &&
		pulse_duration >= 1 && pulse_duration <= 300) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"rx_nb_param:%d,%d,%d;",
		        rtxId,triggering_threshold,pulse_duration);
		send_message();	
	}
	
}

// ===== RX_BIN_ENABLE =====
void TCI::attach_rx_bin_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_bin_enable_event=_eventHandler;
}

void TCI::set_rx_bin_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_bin_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_bin_enable:%d,false;",rtxId);
	send_message();			  
}


// ===== RX_NR_ENABLE =====
void TCI::attach_rx_nr_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_nr_enable_event=_eventHandler;
}

void TCI::set_rx_nr_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_nr_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_nr_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_ANC_ENABLE =====
void TCI::attach_rx_anc_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_anc_enable_event=_eventHandler;
}

void TCI::set_rx_anc_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_anc_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_anc_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_ANF_ENABLE =====
void TCI::attach_rx_anf_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_anf_enable_event=_eventHandler;
}

void TCI::set_rx_anf_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_anf_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_anf_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_APF_ENABLE =====
void TCI::attach_rx_apf_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_apf_enable_event=_eventHandler;
}

void TCI::set_rx_apf_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_apf_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_apf_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_DSE_ENABLE =====
void TCI::attach_rx_dse_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_dse_enable_event=_eventHandler;
}

void TCI::set_rx_dse_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_dse_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_dse_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== RX_NF_ENABLE =====
void TCI::attach_rx_nf_enable_event(eventHandlerRigFunction _eventHandler) {
	do_rx_nf_enable_event=_eventHandler;
}

void TCI::set_rx_nf_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"rx_nf_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"rx_nf_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== LOCK =====
void TCI::attach_lock_event(eventHandlerRigFunction _eventHandler) {
	do_lock_event=_eventHandler;
}

void TCI::set_lock(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"lock:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"lock:%d,false;",rtxId);
	send_message();			  
}

// ===== SQL_ENABLE =====
void TCI::attach_sql_enable_event(eventHandlerRigFunction _eventHandler) {
	do_sql_enable_event=_eventHandler;
}

void TCI::set_sql_enable(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (value)
		sprintf(outgoing_message,"sql_enable:%d,true;",rtxId);
	else
		sprintf(outgoing_message,"sql_enable:%d,false;",rtxId);
	send_message();			  
}

// ===== SQL_LEVEL =====
void TCI::attach_sql_level_event(eventHandlerRigFunction _eventHandler) {
	do_sql_level_event=_eventHandler;
}

void TCI::set_sql_level(int rtxId, int db_level) {	

	if (rtxId>=0 && rtxId<N_MAX_RTX && 
		db_level >= -140 && db_level <=0) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"sql_level:%d,%d;",rtxId,db_level);
		send_message();	
	}
	
}

//
// ********************** UNIDIRECTIONAL CONTROL COMMANDS ***********************
//

// ===== TX_ENABLE =====
void TCI::attach_tx_enable_event(eventHandlerRigFunction _eventHandler) {
	do_tx_enable_event=_eventHandler;
}

// ===== CW_MACROS_SPEED_UP =====
void TCI::set_cw_macros_speed_up(int speed) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"cw_macros_speed_up:%d;",speed);
	send_message();	
}

// ===== CW_MACROS_SPEED_DOWN =====
void TCI::set_cw_macros_speed_down(int speed) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"cw_macros_speed_down:%d;",speed);
	send_message();	
}

// ===== SPOT =====
void TCI::set_spot(char *callsign, char *mode, unsigned int frequency, unsigned int argb_color) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"spot:%s,%s,%u,%u,none;",callsign,mode,frequency,argb_color);
	send_message();	
}

// ===== SPOT_DELETE =====
void TCI::delete_spot(char *callsign) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"spot_delete:%s;",callsign);
	send_message();	
}

// ===== IQ_SAMPLERATE =====
void TCI::attach_iq_samplerate_event(eventHandlerFunction _eventHandler) {
	do_iq_samplerate_event=_eventHandler;
}

void TCI::set_iq_samplerate(int value) {	
	if (value == 48000 || value == 96000 ||
	    value == 192000 || value == 384000) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"iq_samplerate:%d;",value);
		send_message();
	}
}

int TCI::get_iq_samplerate() {
	return iq_samplerate;
}

// ===== AUDIO_SAMPLERATE =====
void TCI::attach_audio_samplerate_event(eventHandlerFunction _eventHandler) {
	do_audio_samplerate_event=_eventHandler;
}

void TCI::set_audio_samplerate(int value) {	
	if (value == 8000 || value == 12000 ||
	    value == 24000 || value == 48000) {
		memset(outgoing_message, 0, sizeof(outgoing_message));
		sprintf(outgoing_message,"audio_samplerate:%d;",value);
		send_message();
	}
}

int TCI::get_audio_samplerate() {
	return audio_samplerate;
}

// ===== IQ_START/IQ_STOP =====
void TCI::attach_iq_start_stop_event(eventHandlerRigFunction _eventHandler) {
	do_iq_start_stop_event=_eventHandler;
}

void TCI::iq_start(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"iq_start:%d;",rtxId);
	send_message();
}

void TCI::iq_stop(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"iq_stop:%d;",rtxId);
	send_message();
}

// ===== AUDIO_START/AUDIO_STOP =====
void TCI::attach_audio_start_stop_event(eventHandlerRigFunction _eventHandler) {
	do_audio_start_stop_event=_eventHandler;
}

void TCI::audio_start(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"audio_start:%d;",rtxId);
	send_message();
}

void TCI::audio_stop(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"audio_stop:%d;",rtxId);
	send_message();
}

// ===== LINE_OUT_START/LINE_OUT_STOP =====
void TCI::attach_line_out_start_stop_event(eventHandlerRigFunction _eventHandler) {
	do_line_out_start_stop_event=_eventHandler;
}

void TCI::line_out_start(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"line_out_start:%d;",rtxId);
	send_message();
}

void TCI::line_out_stop(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"line_out_stop:%d;",rtxId);
	send_message();
}

// ===== LINE_OUT_RECORDER_START =====
void TCI::attach_line_out_recorder_start_event(eventHandlerRigFunction _eventHandler) {
	do_line_out_recorder_start_event=_eventHandler;
}

void TCI::line_out_recorder_start(int rtxId, int duration) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"line_out_recorder_start:%d,%d;",rtxId,duration);
	send_message();
}

// ===== LINE_OUT_RECORDER_SAVE =====
void TCI::attach_line_out_recorder_save_event(eventHandlerRigFunction _eventHandler) {
	do_line_out_recorder_save_event=_eventHandler;
}

void TCI::line_out_recorder_save(int rtxId, char *filename) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"line_out_recorder_save:%d,%s;",rtxId,filename);
	send_message();
}

// ===== LINE_OUT_RECORDER_BREAK =====
void TCI::attach_line_out_recorder_break_event(eventHandlerRigFunction _eventHandler) {
	do_line_out_recorder_break_event=_eventHandler;
}

void TCI::line_out_recorder_break(int rtxId) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"line_out_recorder_break:%d;",rtxId);
	send_message();
}

// ===== SPOT_CLEAR =====
void TCI::spot_clear() {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"spot_clear;");
	send_message();
}


//
// ********************** NOTIFICATIONS COMMANDS ***********************
//

// ===== CLICKED_ON_SPOT_EVENT =====
void TCI::attach_clicked_on_spot_event(eventHandlerFunction _eventHandler) {
	do_clicked_on_spot_event = _eventHandler;
}

spot TCI::get_clicked_spot() {
	return clicked_spot;
}

// ===== RX_CLICKED_ON_SPOT_EVENT =====
void TCI::attach_rx_clicked_on_spot_event(eventHandlerFunction _eventHandler) {
	do_rx_clicked_on_spot_event = _eventHandler;
}


// ===== TX_FOOTSWITCH =====
void TCI::attach_tx_footswitch_event(eventHandlerRigFunction _eventHandler) {
	do_tx_footswitch_event=_eventHandler;
}

// ===== TX_FREQUENCY =====
void TCI::attach_tx_frequency_event(eventHandlerFunction _eventHandler) {
	do_tx_frequency_event=_eventHandler;
}

unsigned int TCI::get_tx_frequency() {
	return tx_frequency;
}

// ===== APP_FOCUS =====
void TCI::attach_app_focus_event(eventHandlerFunction _eventHandler) {
	do_app_focus_event=_eventHandler;
}

bool TCI::is_app_focus() {
	return app_focus;
}

// ===== SET_IN_FOCUS =====
void TCI::set_in_focus() {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"set_in_focus;");
	send_message();	
}

// ===== KEYER =====
void TCI::set_keyer(int rtxId, bool value) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	sprintf(outgoing_message,"keyer:%d,%s;",
	        rtxId,
			value ? "true" : "false");
	send_message();	
}

// ===== RX_SENSORS_ENABLE =====
void TCI::set_rx_sensors_enable(bool enable, int interval) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (interval>=30 && interval<=1000) {

		if (enable) {
			sprintf(outgoing_message,"rx_sensors_enable:true,%d;",interval);
			rx_sensors_enable = true;
			rx_sensors_interval = interval;
		} else {
			sprintf(outgoing_message,"rx_sensors_enable:false;");		
			rx_sensors_enable = false;
			rx_sensors_interval = 0;
		}

		send_message();	
	} else {
		Serial.printf("Invalid rx_sensors sending interval:%d - Allowed values[30-1000] ms",interval);
	}
	
}

bool TCI::is_rx_sensors_enable() {
	return rx_sensors_enable;
}

int TCI::get_rx_sensors_interval() {
	return rx_sensors_interval;
}

// ===== RX_SENSORS =====
void TCI::attach_rx_sensors_event(eventHandlerRigVfoFunction _eventHandler) {
	do_rx_sensors_event=_eventHandler;
}

// ===== TX_SENSORS_ENABLE =====
void TCI::set_tx_sensors_enable(bool enable, int interval) {
	memset(outgoing_message, 0, sizeof(outgoing_message));
	if (interval>=30 && interval<=1000) {		
		if (enable) {
			sprintf(outgoing_message,"tx_sensors_enable:true,%d;",interval);
			tx_sensors_enable = true;
			tx_sensors_interval = interval;
		} else {
			sprintf(outgoing_message,"tx_sensors_enable:false;");		
			tx_sensors_enable = false;
			tx_sensors_interval = 0;
		}

		send_message();	
	} else {
		Serial.printf("Invalid tx_sensors sending interval:%d - Allowed values[30-1000] ms",interval);
	}
	
}

bool TCI::is_tx_sensors_enable() {
	return tx_sensors_enable;
}

int TCI::get_tx_sensors_interval() {
	return tx_sensors_interval;
}

// ===== TX_SENSORS =====
void TCI::attach_tx_sensors_event(eventHandleTxSensorsFunction _eventHandler) {
	do_tx_sensors_event=_eventHandler;
}

// ===== AUDIO_STREAM_SAMPLE_TYPE =====
void TCI::attach_audio_stream_sample_type_event(eventHandlerRigFunction _eventHandler) {
	do_audio_stream_sample_type_event=_eventHandler;
}

int TCI::get_audio_stream_sample_type() {
	return audio_stream_sample_type;
}

void TCI::set_audio_stream_sample_type(int type) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	char sType[10];
	switch (type) {
		case 1:
			sprintf(outgoing_message,"audio_stream_sample_type:int16;");
			break;
		case 2:
			sprintf(outgoing_message,"audio_stream_sample_type:int24;");
			break;
		case 3:
			sprintf(outgoing_message,"audio_stream_sample_type:int32;");
			break;
		case 4:
			sprintf(outgoing_message,"audio_stream_sample_type:int16;");			
			break;				
		default:
			Serial.printf("audio_stream_sample_type: invalid type\n");		
	}
	if (type >= 1 && type <=4) 
		send_message();	
}


// ===== AUDIO_STREAM_CHANNELS =====
void TCI::attach_audio_stream_channels_event(eventHandlerRigFunction _eventHandler) {
	do_audio_stream_channels_event=_eventHandler;
}

int TCI::get_audio_stream_channels() {
	return audio_stream_channels_count;
}

void TCI::set_audio_stream_channels(int val) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	
	if (val>=1 && val<=2) {		
		sprintf(outgoing_message,"audio_stream_channels:%d;",val);
		send_message();
	} else {
		Serial.printf("audio_stream_sample_type: invalid value\n");		
	}	
			
}

// ===== AUDIO_STREAM_SAMPLES =====
void TCI::attach_audio_stream_samples_event(eventHandlerRigFunction _eventHandler) {
	do_audio_stream_samples_event=_eventHandler;
}

int TCI::get_audio_stream_samples() {
	return audio_stream_samples;
}

void TCI::set_audio_stream_samples(int val) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	
	if (val>=100 && val<=2048) {		
		sprintf(outgoing_message,"audio_stream_samples:%d;",val);
		send_message();
	} else {
		Serial.printf("audio_stream_samples: invalid value\n");
	}		
}

// ===== DIGL_OFFSET =====
void TCI::attach_digl_offset_event(eventHandlerRigFunction _eventHandler) {
	do_digl_offset_event=_eventHandler;
}

int TCI::get_digl_offset() {
	return digl_offset;
}

void TCI::set_digl_offset(int val) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	
	if (val>=0 && val<=4000) {		
		sprintf(outgoing_message,"digl_offset:%d;",val);
		send_message();
	} else {
		Serial.printf("digl_offset: invalid value\n");		
	}
}


// ===== DIGU_OFFSET =====
void TCI::attach_digu_offset_event(eventHandlerRigFunction _eventHandler) {
	do_digu_offset_event=_eventHandler;
}

int TCI::get_digu_offset() {
	return digl_offset;
}

void TCI::set_digu_offset(int val) {	
	memset(outgoing_message, 0, sizeof(outgoing_message));
	
	if (val>=0 && val<=4000) {		
		sprintf(outgoing_message,"digu_offset:%d;",val);
		send_message();
	} else {
		Serial.printf("digu_offset: invalid value\n");		
	}
}
