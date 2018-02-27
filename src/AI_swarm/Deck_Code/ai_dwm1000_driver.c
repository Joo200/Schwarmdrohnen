#include "../Deck_Header/ai_dwm1000_driver.h"
#include "../../../vendor/CMSIS/CMSIS/Driver/Include/Driver_SPI.h"
#include "stm32f4xx_spi.h"
#include "deck_spi.h"
#include "stm32f4xx_exti.h"	//wird benoetigt um externe interrupts zu initialisieren
#include "stm32f4xx_exti.h"	//wird benoetigt um externe interrupts zu initialisieren
#include "../ai_datatypes.h"
#include "stm32f4xx.h"
#include "../ai_task.h"
#include "../../vendor/unity/extras/fixture/src/unity_fixture_malloc_overrides.h"

//hier wird deck_spi.h/deck_spi.c angewendet (in src/deck/api/...)

//helper Functions:
void spiStart(){
	spiBeginTransaction(BaudRate);
}

void spiStop(){
	spiEndTransaction();
}

//inhalt von locodec.c init (ab Z.312) inspiriert (und angepasst)
bool setup_dwm1000_communication(){
	
	// ---- Aus ST-Doku: ----
	/*
	In order to use an I / O pin as an external interrupt source, follow steps
		below :
	(#) Configure the I / O in input mode using GPIO_Init()
		(#) Select the input source pin for the EXTI line using SYSCFG_EXTILineConfig()
		(#) Select the mode(interrupt, event) and configure the trigger
		selection(Rising, falling or both) using EXTI_Init()
		(#) Configure NVIC IRQ channel mapped to the EXTI line using NVIC_Init()

		[..]
	(@) SYSCFG APB clock must be enabled to get write access to SYSCFG_EXTICRx
		registers using RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	*/

	EXTI_InitTypeDef EXTI_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	spiBegin();

	// Init IRQ input
	bzero(&GPIO_InitStructure, sizeof(GPIO_InitStructure));
	GPIO_InitStructure.GPIO_Pin = GPIO_PIN_IRQ; //GPIO_PIN_11
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	//GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;

	GPIO_Init(GPIO_PORT, &GPIO_InitStructure);

	// Init reset output
	GPIO_InitStructure.GPIO_Pin = GPIO_PIN_RESET; //GPIO_PIN_10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

	GPIO_Init(GPIO_PORT, &GPIO_InitStructure);
	
	//Interrupt Initstruct vorbereiten
	EXTI_InitStructure.EXTI_Line = EXTI_LineN;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;

	EXTI_Init(&EXTI_InitStructure);

	// Enable interrupt
	NVIC_InitStructure.NVIC_IRQChannel = EXTI_IRQChannel;	//EXTI15_10_IRQn
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = NVIC_LOW_PRI;	//13
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// Reset the DW1000 chip
	GPIO_WriteBit(GPIO_PORT, GPIO_PIN_RESET, 0);
	vTaskDelay(M2T(10));
	GPIO_WriteBit(GPIO_PORT, GPIO_PIN_RESET, 1);
	vTaskDelay(M2T(10));

	return 1;
}

bool dwm1000_SendData(void * data, int lengthOfData, e_message_type_t message_type, char targetID /*Adressen?, ...*/) {
	spiStart();

	//1. Aufbauen der Transmit Frame f�r den SPI Bus an den DWM1000
	void *sendData;
	int messageSize = lengthOfData + sizeof(char) + sizeof(e_message_type_t);
	sendData = malloc(messageSize);	// zwei bytes Extra um Art der Nachricht und Name des Senders beizufügen
	if (sendData == NULL) {
		return false;		//kein Mem mehr verfügbar --> Funktion wird nicht Ausgeführt
	}
	char senderID = my_ai_name; 

	*(char*)sendData = senderID;																	//Sender ID ist erstes Byte der Nachricht
	*(char*)((int*)sendData + sizeof(char)) = targetID;												//targedID ist ab zweites Byte der Nachricht
	*(e_message_type_t*)((int*)sendData + 2*sizeof(char)) = message_type;						//message ytpe ist ab drittes Byte der Nachricht
	for (int i = 0; i < lengthOfData; i++)															//jedes byte einzeln auf alloc Speicher schreiben
	{
		*((char*)sendData + 2*sizeof(char) + sizeof(e_message_type_t) + i) = *((char*)data + i);
	}

	//2. Data auf Transmit Data Buffer Register packen
	unsigned char instruction = WRITE_TXBUFFER;
	unsigned char receiveByte = 0x00;

	void *placeHolder = malloc(lengthOfData);
	
	for (int i = 0; i < lengthOfData; i++)
	{
		*((char*)placeHolder + i) = 0;
	}
	
	//spiExchange(size_t length, const uint8_t * data_tx, uint8_t * data_rx)		
	spiExchange(1, &instruction, placeHolder);	//instruction Schicken - write txbuffer

	spiExchange(lengthOfData, sendData, placeHolder);

	//3. System Control aktualisieren
	instruction = READ_SYS_CTRL;
	spiExchange(1, &instruction, &receiveByte);	//instruction: ich will sysctrl lesen

	void * sysctrl = malloc(5);
	spiExchange(5, placeHolder, sysctrl);		//syscontrol lesen


	*(int*)sysctrl |= SET_TRANSMIT_START;		//neuen Inhalt für syscontrol erstellen

	instruction = WRITE_SYS_CTRL;			
	spiExchange(1, &instruction, placeHolder);	//instruction: ich will syscontrol schreiben

	spiExchange(5, sysctrl, placeHolder);		//syscontrol schreiben

	//4. Sendung ueberpruefen (Timestamp abholen?, ...)
		//.. noch keine Relevanten Dinge eingefallen

	//5. Resourcen freigeben
	free(sendData);
	free(placeHolder);
	free(sysctrl);

	spiStop();

	return 1;
}

e_message_type_t dwm1000_ReceiveData(st_message_t *data) {
	//1. Receive Buffer Auslesen

	//2. Receive Enable Setzten
	//3. Art der Nachricht entschluesseln
	//4. Auf Data schreiben
	//5. Art der Nachricht zurueckgeben 
	e_message_type_t retType;
	retType = DISTANCE_REQUEST;

	return retType;

}

float dwm1000_getDistance(double nameOfOtherDWM) {
	// Beschreibung für eine Drohne (gesammtes Vorgehen)
	//1. Nachrichten an Partner schicken (Master)
	//2. Partner antwortet  mit seinem Timestamp (Slave)
	//3. Master empfägt Slave-Timestamp
	//4. Errechnung der Response Time
	//5. (Gestoppte Zeit - (TransmitTimestamp_Ziel - ReceiveTimestamp_Ziel))/2 * Lichtgeschw = Abstand
	// Danach weiß der Master, Initiator den Abstand 

	float distance = 0;
	return distance;
}

void dwm1000_immediateDistanceAnswer(char id_requester) {
	//1. Funktion aktiviert, nach Distance request Eingang
	//2. Antworten, damit requester Zeit stopppen kann

	//dwm1000_SendData(void * data, int lengthOfData, enum e_message_type_t message_type, char targetID /*Adressen?, ...*/);

	//Zusatz: Zeit zwischen Receive Timestamp und Transmit Timestamp an requester schicken, damit von gestoppter Zeit abgezogen werden kann
	//Receive Timestamp - Transmit Timestamp
}

void dmw1000_sendProcessingTime(char id_requester) {
	
	spiStart();
	
	//1. Timestamp TX lesen, Register 0x17 Timestamp von bit 0-39
	void *txTimestamp;
	int txStampSize = 5;			//5 Bytes für Timestaqmp reservieren
	txTimestamp = malloc(txStampSize);

	void *placeholder;
	placeholder = malloc(txStampSize);

	unsigned char instruction = READ_TX_TIMESTAMP;

	//Instruction Transmitten	
	spiExchange(1, &instruction, placeholder);

	//Placeholder mit 0 fuellen
	for (int i = 0; i < txStampSize; i++)
	{
		*((char*)placeholder + i) = 0;
	}

	//Register auslesen
	spiExchange(txStampSize, placeholder, txTimestamp);

	//2. Timestamp RX lesen, Register 0x15 Timestamp von bit 0-39
	void *rxTimestamp;
	int rxStampSize = 5;			//5 Bytes für Timestaqmp reservieren
	rxTimestamp = malloc(rxStampSize);

	instruction = READ_RX_TIMESTAMP;

	//Instruction Transmitten	
	spiExchange(1, &instruction, placeholder);
	
	//Placeholder mit 0 fuellen
	for (int i = 0; i < txStampSize; i++)
	{
		*((char*)placeholder + i) = 0;
	}

	//Register auslesen
	spiExchange(rxStampSize, placeholder, rxTimestamp);

	//3. Differenz bestimmen, Rx-Tx, Ergebnis in 15,65 Pico sek

	double result = rxTimestamp - txTimestamp;

	//4. an requester schicken

	void * pups = malloc((int)result);
	//dwm1000_SendData(void * result, 5, enum e_message_type_t message_type, char targetID /*Adressen?, ...*/);

	free(pups);
	free(txTimestamp);
	free(placeholder);
	free(rxTimestamp);
	spiStop();
}

e_interrupt_type_t dwm1000_EvalInterrupt()
{
	//5 Bytes fuer System Event Status Register reservieren
	void *sesrContents;
	int registerSize = 5;			
	sesrContents = malloc(registerSize);	

	void *placeholder;
	placeholder = malloc(registerSize);

	unsigned char instruction = READ_SESR;

	spiStart();
	
	//Instruction Transmitten (an Slave)	
	spiExchange(1, &instruction, placeholder);		

	//Placeholder mit 0 fuellen
	for (int i = 0; i < registerSize; i++)
	{
		*((char*)placeholder + i) = 0;
	}

	//Register auslesen
	spiExchange(registerSize, placeholder, sesrContents);

	//Gelesenes Register auswerten
	e_interrupt_type_t retVal = FAILED_EVAL;
	unsigned char TFSMask = MASK_TRANSMIT_FRAME_SENT;
	short RFSMask = MASK_RECEIVE_FRAME_SENT;

	//ist Transmit Frame Sent gesetzt?
	if ((*(char*)sesrContents & TFSMask) > 1)		//char, weil Mask 1 Byte lang
	{
		retVal = TX_DONE;
	}

	//ist Receive Frame Sent gesetzt?
	else if ((*(short*)sesrContents & RFSMask) > 1)	//short, weil Mask 2 Byte lang
	{
		retVal = RX_DONE;
	}

	//Resourcen freigeben
	free(placeholder);
	free(sesrContents);
	spiStop();

	return retVal;
}

void dwm1000_init() {

	spiStart();	//fuer Mutexinteraktion genutzt	
	
	//Init Transmission
	
	unsigned char instruction = READ_TFC;		//Read Transmission Frame Control Register
	spiExchange(1, &instruction, ...);
	//WRITE_INIT_TX_FCTRL


	instruction = READ_SYS_CTRL;
	spiExchange(1, &instruction, ...);

	instruction = READ_SYS_STATUS;
	spiExchange(1, &instruction, ...);


	// -------- Init SPI --------

	//Baud Rate
		//Baudrate Nachricht aus newConfig erstellen

	spiExchange(0,0,0);

	// -------- Init UWB --------

	//PAN Identifier
		//Identifeier Nachricht aus newConfig erstellen

	spiExchange(0,0,0);



	spiStop();	//fuer Mutexinteraktion genutzt	
}

void __attribute__((used)) EXTI11_Callback(void)
{
	EXTI_ClearITPendingBit(EXTI_Line4);
	DMW1000_IRQ_Flag = 1;	//aktiviert synchrone "ISR" in ai_task.c
}
