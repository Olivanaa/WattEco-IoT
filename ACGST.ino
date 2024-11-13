#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h> 
#include <time.h>
#include <AsyncTCP.h>
#include <PubSubClient.h>   
#include <ArduinoJson.h>

// Configurações de dispositivos:

#define pinDHT 25
#define tipoDHT DHT22

#define potCUR 32
#define potTEMP 35
#define potUMID 34
#define potLUM 33

#define pinLDR 27

#define boardLED 2

DHT dht(pinDHT, tipoDHT);

// Identificador do local do sensor
const char* ID = "Fiap Paulista"; 
// Identificador do sensor
const char* SensorID = "ACGST";

// Credenciais WiFi
const char* SSID = "Wokwi-GUEST";
const char* PASSWORD = ""; 

// MQTT
const char* BROKER_MQTT = "74.179.84.66"; //URL do broker MQTT que se deseja utilizar
int BROKER_PORT = 1883; // Porta do Broker MQTT
const char* mqttServer = "74.179.84.66";
const int mqttPort = 1880;
const char* mqttUser = "gs2024"; // Usuário do Broker
const char* mqttPassword = "q1w2e3r4"; // Senha do Broker
#define TOPICO_SUBSCRIBE "ACGST"     //tópico MQTT de escuta
#define TOPICO_PUBLISH   "ACGST"    //tópico MQTT de envio de informações para Broker

// Variáveis de data e hora 
#define NTP_SERVER "pool.ntp.org"   
#define TZ             -3                // (utc+) TZ in hours
#define DST_MN         0               // Horário de verão em alguns países...
#define GMT_OFFSET_SEC 3600 * TZ        // Conversão de formatos
#define DAYLIGHT_OFFSET_SEC 60 * DST_MN // Conversão de formatos
#define NTP_MIN_VALID_EPOCH 1533081600  
String dateTimeStr(time_t epochtime, char* pattern = (char *)"%d-%m-%Y %H:%M:%S");

String hora;
String Data;
tm        *NOW_TM;                      
time_t     NOW;
int Second;
int Minute;
int Hour;
int Day;
int Month;
int Year;
int Weekday;
char DayName[12];
char MonthName[12];
char Time[10];
char Date[12];

// Configurações MQTT
WiFiClient espClient; // Cria o objeto espClient
PubSubClient MQTT(espClient); // Instancia o cliente MQTT passando o objeto espClient
char EstadoSaida = '0';  // Variável que armazena o estado atual da saída
char buffer[256];

// Declaração de variáveis:
float temp, umid, voltagem, corrente, voltage, resistance, lux;
int valorAnalogTemp, valorAnalogUmid, valorAnalogCorrente, valorAnalogLdr, lumi;
float tempSim, umiSim, voltagemSim, lumSim;

// Ponto de zero corrente (1,65V no ESP32 com sensor) 3.3/2
const double pontoZero = 1.65; 

// Fator de escala para sensor de corrente
const double scale_factor = 0.1; // 20A

const float GAMMA = 0.7;
const float RL10 = 50;

//Função: inicializa parâmetros de conexão MQTT(endereço do broker, porta e seta função de callback)
void initMQTT() {
    MQTT.setServer(BROKER_MQTT, BROKER_PORT);
    while (!MQTT.connected()) {
        Serial.println("Conectando ao Broker MQTT...");
        if (MQTT.connect(SensorID, mqttUser, mqttPassword)) {
            Serial.println("Conectado ao MQTT!");
        } else {
            Serial.print("Falha ao conectar, estado: ");
            Serial.println(MQTT.state());
            delay(2000);  
        }
    }
}


//Função: inicializa e conecta-se na rede WiFi desejada
void initWiFi(){
WiFi.begin(SSID, PASSWORD);
delay(2000);
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}
Serial.println("---------------------");  
Serial.println("WiFi conectado...    ");
Serial.println("---------------------");
Serial.print("ESP Board MAC Address:  ");
Serial.println(WiFi.macAddress());
Serial.print("Conectado a rede ");
Serial.println(SSID);
Serial.print("O endereço IP é: ");
Serial.println(WiFi.localIP());

}

void reconectWiFi(){
    WiFi.begin(SSID, PASSWORD);
    delay(3000);
    Serial.println("---------------------");  
    Serial.println("WiFi conectado...    ");
    Serial.println("---------------------");
    Serial.print("Conectado em: ");
    Serial.println(SSID);
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP()); 
    Serial.print("ESP Board MAC Address:  ");
    Serial.println(WiFi.macAddress()); 
    Serial.println("----------------------");    
}

//Função: verifica o estado das conexões WiFI e ao broker MQTT. 
//Em caso de desconexão (qualquer uma das duas), a conexão é refeita.
void VerificaConexoesWiFIEMQTT(void)
{
    if (!MQTT.connected()) 
    Serial.println("Conectando ao Broker MQTT...");
    
    initMQTT()  ;
    reconectWiFi(); //se não há conexão com o WiFi, a conexão é refeita
}

//Função: envia ao Broker o estado atual do output 
void EnviaEstadoOutputMQTT(void)
{
    MQTT.publish(TOPICO_PUBLISH, buffer);
    Serial.println("Informação enviada ao Broker MQTT!!");
}

void initNTP()
{
    //#ifdef ARDUINO_ARCH_ESP32
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    //#else
    //configTime(MYTZ, NTP_SERVER);
    //#endif
    
    unsigned long t0 = millis();
    Serial.print ("Synching Time over NTP ");
    while((NOW = time(nullptr)) < NTP_MIN_VALID_EPOCH) 
    {
        Serial.println("Sincronizando NTP");
        delay(20);
        Serial.print(".");
    }
    Serial.println("");
    unsigned long t1 = millis() - t0;
    Serial.println("NTP time first synch took " + String(t1) + "ms");
}

void setNOW()
{
    NOW = time(nullptr);
}

// Breaks NOW into tm structure and updates NOW_TM global var
void setNOW_TM()
{
    NOW_TM  = localtime(&NOW);  
    Second  = NOW_TM->tm_sec;
    Minute  = NOW_TM->tm_min;
    Hour    = NOW_TM->tm_hour;
    Weekday = NOW_TM->tm_wday + 1 ;
    Day     = NOW_TM->tm_mday;
    Month   = NOW_TM->tm_mon + 1;
    Year    = NOW_TM->tm_year + 1900;      
    strftime (DayName , 12, "%A", NOW_TM); 
    strftime (MonthName, 12, "%B", NOW_TM);
    strftime (Time,10, "%T", NOW_TM);
    strftime (Date,12, "%Y-%m-%d", NOW_TM);
}

String dateTimeStr(time_t epochtime, char* pattern)
{
    tm *lt;
    lt = localtime(&epochtime);
    char buff[30];
    strftime(buff, 30, pattern, lt);
    return buff;
}

void piscaLED()
{
    digitalWrite(boardLED, HIGH);
    delay(300);
    digitalWrite(boardLED, LOW);  
}

void setup() {
  Serial.begin(115200);  

  pinMode(boardLED, OUTPUT);    
  digitalWrite(boardLED, LOW); 

  dht.begin();
  initWiFi();
  initMQTT();
  initNTP();  
}

void loop() {
  initWiFi();
  delay(1000);
  
  // Ler dados do DHT
  temp = dht.readTemperature();
  umid = dht.readHumidity();

  //Ler dados do LDR
  lumi = analogRead(pinLDR);
  
  voltage = lumi / 4095.0 * 3.3;  
  resistance = (2000  * (3.3 - voltage)) / voltage; 
  lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA));

  // Simulação de dados
  valorAnalogTemp = analogRead(potTEMP);
  valorAnalogUmid = analogRead(potUMID);
  valorAnalogCorrente = analogRead(potCUR);
  valorAnalogLdr = analogRead(potLUM);

  // Serial.print("valorAnalogTemp: "); Serial.println(valorAnalogTemp);
  // Serial.print("valorAnalogUmid: "); Serial.println(valorAnalogUmid);
  // Serial.print("valorAnalogCorrente: "); Serial.println(valorAnalogCorrente);
  // Serial.print("valorAnalogLdr: "); Serial.println(valorAnalogLdr);

  tempSim = map(valorAnalogTemp, 0, 4095, 0, 50);  
  umiSim = map(valorAnalogUmid, 0, 4095, 0, 100); 
  lumSim = map(valorAnalogLdr, 0, 4095, 0.1, 100000);
  voltagemSim = map(valorAnalogCorrente, 0, 4095, 0, 3300)/1000;
  corrente = (voltagemSim - pontoZero) / scale_factor; 

  Serial.print("Temp: "); Serial.println(tempSim);
  Serial.print("Umid: "); Serial.println(umiSim);
  Serial.print("Corrente: "); Serial.println(corrente);
  Serial.print("Voltagem: "); Serial.println(voltagemSim);
  Serial.print("Lux: "); Serial.println(lumSim);
  Serial.print("Lux sensor: "); Serial.println(lux);
  Serial.print("Temp sensor: "); Serial.println(temp);
  Serial.print("Umid sensor: "); Serial.println(umid);

  setNOW();
  setNOW_TM();
  
  Data = String(Date) + " " + String(Time);

  StaticJsonDocument<200> doc;
  doc["ID"] = ID;
  doc["Sensor"] = SensorID;
  doc["Time"] = Data;
  doc["Temperatura"] = tempSim;
  doc["Umidade"] = umiSim;
  doc["Voltagem"] = voltagemSim;
  doc["Corrente"] = corrente;
  doc["Iluminancia"] = lumSim;
  
  serializeJson(doc, buffer);
  Serial.println(buffer);

  initMQTT();
  EnviaEstadoOutputMQTT();    
  delay(10000);   
}