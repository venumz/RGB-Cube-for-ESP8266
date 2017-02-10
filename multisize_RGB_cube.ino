/* 
Multi size RGB Led cube for WIFI modulde ESP8266, by Venum. 
This cube works with any cube size (even 1x1x1, silly one I know).
I couldn't do it without Kevin Darrah's help (www.kevindarrah.com), his videos were extremely useful.

The RGB leds must be common anode ones, and they are wired connecting the anodes on each level together, then each column has its cathodes 
connected together. So LED 0x0x0 has each cathode connected all the way up to  cubeSizex0x0.
The cathodes are sinked directly to the shift register pins, but anodes are driven with p-channel mosfets. I'm using 1k resistors for the cathodes so
the whole system should be fine.

For some reason BAM wasn't working as espected, I noticed issues in the time each anode was low when stepping for example from brightness 7 to 8 
(much less brightness for step 8 than 7). So I decided to just use integer counters from 0 to 15 and leave the anode low n/15 of the time. 

Notice that all counters start from zero, so levels, rows, columns, registers and anodes start from zero, 
*/

byte data=D1;
byte clk=D2;
byte latch=D3;
const int cubeSize=2;
const double timerInterval=150; // uSeconds for the timer to kick in to change the anode level and starting the pseudo BAM
bool anodesActiveLow=true;  // Anodes driven by p-channel mosfets


/**** DO NOT TOUCH THE VALUES BELOW *****/
const int registerNumber=ceil((pow(cubeSize,2)*3 + cubeSize)/8.0); // total registers plus the anodes so +cubeSize
volatile int currentLevel=0;
double chipsetClock=80000000;
const int refreshTime=timerInterval/15; // microseconds for each step of the pseudo BAM
unsigned long int timerCycle;
byte registers[registerNumber]={}; // 
int ledsState[cubeSize][cubeSize][cubeSize][3]={}; // we keep the state of our leds here
int anodesRegNum; // tracks the number of register containing anodes
int anodesReg[2][3]; // tracks the anode composition per register (id refister, number of anodes, bit for the first anode inside the register)
byte anodes[registerNumber][8]={}; // tracks all the anodes for each register 
int ledNumber,colorNumber;
/******************************/

/*
 * Function prototypes
 */
void updateLatch();
void clearRegisters();
void deactAnodes();
void prepareAnodes();
void actAnode(int);
void showRegisters();
void showAnodes();
void refreshCube();
void bam(int,int,int,int,int);
/********************/

void timer0_ISR (void) {

  /* The order of colors matters, you must prepare the cube with RGB color order:
   *  RGBRGBRG BRGBRGBR ....
   * 
   *  This is going to multiplex the signal and loop for each level.
   *  Anodes (levels) are going to be powered 1/levels of the time.
   *  then we are going to modulate the brightness of the leds looping 16 times (sort of 4 bit modulation) for each level.
   *  When a color is going to light up, it's cathode must be LOW and the relative anode must be HIGH, anodes are powered by
   *  p-mosfets so the bit in the register that control the anode must be LOW.
   * 
   *  Here we calc the byte (register) and the bit inside the register for the red color.
   *  if the bit is higher than 6 our led colors are splitted between 2 registers.
   */
   
  clearRegisters();
  actAnode(currentLevel);
  for(int bamCounter=0;bamCounter<15;bamCounter++){
      for(int row=0;row<cubeSize;row++){ // rows
      for(int col=0;col<cubeSize;col++){ // cols
        int redRegister=floor((row*3*cubeSize+col*3)/8.0);
        int redBit=(row*3*cubeSize+col*3)-redRegister*8;
        int redBrightness=ledsState[currentLevel][row][col][0];
        int greenBrightness=ledsState[currentLevel][row][col][1];
        int blueBrightness=ledsState[currentLevel][row][col][2];
        bam(bamCounter, redRegister,redBit,redBrightness,greenBrightness,blueBrightness);
      }
    }
   refreshCube();
   delayMicroseconds(refreshTime);
  }

  ++currentLevel;
  if(currentLevel>=cubeSize)
    currentLevel=0;
  timer0_write(ESP.getCycleCount() + timerCycle); 
}


void setup() {
  Serial.begin(115200);

  Serial.println("");
  pinMode(data,OUTPUT);
  pinMode(clk,OUTPUT);
  pinMode(latch,OUTPUT);
  digitalWrite(data,LOW);
  digitalWrite(clk,LOW);
  digitalWrite(latch,LOW); 
  ledNumber=pow(cubeSize,3);
  colorNumber=ledNumber*3;
  Serial.print("Total LEDS: ");
  Serial.println(ledNumber);
  Serial.print("Total Colors: ");
  Serial.println(colorNumber);
  Serial.print("Total registers: ");
  Serial.println(registerNumber);
  prepareAnodes();
  clearRegisters();
  refreshCube();
  showAnodes();
  showRegisters();
  noInterrupts();
  timerCycle=floor(chipsetClock*timerInterval/1000000); // running at 80MHz, then timerCycle=80000000 ==> 1sec
  timer0_isr_init();
  timer0_attachInterrupt(timer0_ISR);
  timer0_write(ESP.getCycleCount() + timerCycle);
  interrupts();
}

void clearRegisters(){
  for(int i=0;i<registerNumber;i++){
    registers[i]=B11111111; 
  }
  deactAnodes();
 }

void prepareAnodes(){
  int currentRegister=floor(pow(cubeSize,2)*3 /8.0);
  int anodeBit=(int(pow(cubeSize,2))*3)%8;
  Serial.print("First register for anodes: ");
  Serial.println(currentRegister);  
  Serial.print("First bit for anodes: ");
  Serial.println(anodeBit);

  int c0,c1=0;
  anodesReg[0][0]=currentRegister; // register ID
  anodesReg[0][1]=1;               // numbers of anodes for this resgister
  anodesReg[0][2]=anodeBit;        //  bit of the first anode inside the register
  if(currentRegister<registerNumber-1){
    anodesReg[1][0]=currentRegister+1;
    anodesReg[1][1]=0;
    anodesReg[1][2]=0;
  }
  anodes[currentRegister][c0++]=1<<anodeBit;
  anodesRegNum=1;
  for(int i=1;i<cubeSize;i++){
    if(anodeBit+i>7){
      anodesRegNum=2;
      ++anodesReg[1][1];
      anodes[currentRegister+1][c1++]=1<<(anodeBit+i-8);
    } else {
      anodes[currentRegister][c0++]=1<<(anodeBit+i);
      ++anodesReg[0][1];
    }
  }
}

void showRegisters(){

  Serial.println("\nREGISTERS");
  for(int c=0;c<registerNumber;c++)
    Serial.println(registers[c],BIN);
  Serial.println("");
}

void showAnodes(){
  if(anodesRegNum>1)
    Serial.println("\nAnodes splitted between 2 registers:");
  else
    Serial.println("\nAnodes contained in 1 register:");   

  for(int c=0;c<registerNumber;c++){
    if(c==anodesReg[0][0] || (anodesRegNum>1 && c==anodesReg[1][0])){
      int temp=(c==anodesReg[0][0])?0:1;
      Serial.print("REGISTER ");
      Serial.print(c);
      Serial.println(" anodes: ");
      for(int i=0;i<anodesReg[temp][1];i++)
        Serial.println(anodes[anodesReg[temp][0]][i],BIN);
    }
  }
  Serial.println("");
}

void deactAnodes(){

  for(int i=0;i<registerNumber;i++){
    if(i==anodesReg[0][0]){
      for(int c=0;c<anodesReg[0][1];c++){
        if(anodesActiveLow)
          registers[i]|=anodes[anodesReg[0][0]][c];
        else
          registers[i]&=~anodes[anodesReg[0][0]][c];     
      } 
    } else if(i==anodesReg[1][0]){
      for(int c=0;c<anodesReg[1][1];c++){
        if(anodesActiveLow)
          registers[i]|=anodes[anodesReg[1][0]][c];
        else
          registers[i]&=~anodes[anodesReg[1][0]][c];
      }
    }
  }
}

void actAnode(int level){

  int anodeCounter=0;
  for(int c=0;c<registerNumber;c++){
    if(c==anodesReg[0][0] || (anodesRegNum>1 && c==anodesReg[1][0])){    
      int temp=c==anodesReg[0][0]?0:1;
      for(int i=0;i<anodesReg[temp][1];i++){
        if(level==anodeCounter && anodesActiveLow)
          registers[c]&=~anodes[anodesReg[temp][0]][i];
        else
          registers[c]|=anodes[anodesReg[temp][0]][i];
        ++anodeCounter;
      }
    }
  }
}

void updateLatch(){
  digitalWrite(latch,HIGH);
  digitalWrite(latch,LOW);
}

void ledOff(int level,int row,int col){
  ledsState[level][row][col][0]=0;
  ledsState[level][row][col][1]=0;
  ledsState[level][row][col][2]=0;
}

void ledOn(int level,int row,int col,int redBrightness,int greenBrightness,int blueBrightness){
  level=level>=cubeSize?0:level;
  row=row>=cubeSize?0:row;
  col=col>=cubeSize?0:col;
  redBrightness=redBrightness>15?0:redBrightness;
  greenBrightness=greenBrightness>15?0:greenBrightness;
  blueBrightness=blueBrightness>15?0:blueBrightness;
  
  ledsState[level][row][col][0]=redBrightness;
  ledsState[level][row][col][1]=greenBrightness;
  ledsState[level][row][col][2]=blueBrightness;

}

void clearLeds(){
  for(int l=0;l<cubeSize;l++)
    for(int c=0;c<cubeSize;c++)
      for(int i=0;i<cubeSize;i++)
        ledOff(l,c,i);
}

void refreshCube(){
    for(int i=registerNumber-1;i>=0;i--){
    shiftOut(data,clk,MSBFIRST,registers[i]);         
  }
  updateLatch(); 
    
}

void bam(int bamCounter, int redRegister, int redBit, int redBrightness,int greenBrightness,int blueBrightness){

  int currentRegister=redRegister;
  int currentBit=redBit; 
  updateRegister(currentRegister,currentBit,bamCounter,redBrightness);
  currentRegister=redBit+1<=7?redRegister:redRegister+1;
  currentBit=redBit+1<=7?redBit+1:0;
  updateRegister(currentRegister,currentBit,bamCounter,greenBrightness);
  currentRegister=redBit+2<=7?redRegister:redRegister+1;
  currentBit=redBit+2<=7?redBit+2:redBit-6;
  updateRegister(currentRegister,currentBit,bamCounter,blueBrightness);

}

void updateRegister(int whichRegister, int whichBit,int counter,int brightness){
  bool pinOn=brightness>0 && brightness>counter;

   if(!pinOn){
    // Led color Off
   registers[whichRegister]|=1<<whichBit;
  } else {
    // Led color On
    registers[whichRegister]&=~(1<<whichBit);
  }
}

void loop() {
  singleLed();
  runAround();
  simple();
  goUp();
  showSides();
 }

/****** ANIMATIONS ****/

void singleLed(){
  ledOn(1,1,1,15,0,0);
  ledOn(0,0,0,15,0,0); 
  delay(200);
}

void simple(){
  for(int c=0;c<=5;c++){
    clearLeds();
  ledOn(0,0,0,15,0,0);
  ledOn(1,1,1,0,0,15);
  delay(200);
  clearLeds();
  ledOn(0,0,1,0,0,15);
  ledOn(1,1,0,15,0,0);
  delay(200);
  }
    for(int c=0;c<=5;c++){
   clearLeds();
  ledOn(0,0,0,15,0,0);
  ledOn(1,1,1,0,0,15);
  delay(100);
  clearLeds();
  ledOn(0,0,1,0,0,15);
  ledOn(1,1,0,15,0,0);
  delay(100);
  clearLeds();
  ledOn(1,0,1,0,0,15);
  ledOn(0,1,0,0,15,0);
  delay(100);
  clearLeds();
  ledOn(1,0,0,0,15,0);
  ledOn(0,1,1,0,0,15);
  delay(100);
  }
}

void goUp(){
int z,c,i,j,k;

for(z=40;z<=80;z+=2){
clearLeds();

for(k=1;k<=15;k++){
  for(i=0;i<=1;i++){
    for(j=0;j<=1;j++){
      ledOn(0,i,j,k,0,0);
    }
  }
  if(z<50)
  delay(54-z);
  else
  delay(4);
}
clearLeds();
for(k=1;k<=15;k++){
  for(i=0;i<=1;i++){
    for(j=0;j<=1;j++){
      ledOn(1,i,j,0,0,k);
    }
  }
  if(z<50)
  delay(54-z);
  else
  delay(4);
}
}
}

void runAround(){
  int i,j,z,k;
  k=100;
  for(i=0;i<=7;i++){
   // k=k-10*i;
  ledOn(0,0,0,0,0,1+i*2);
  delay(k);
  ledOff(0,0,0);
  ledOn(0,0,1,0,0,1+i*2);
   delay(k);
   ledOff(0,0,1);   
  ledOn(0,1,1,0,0,1+i*2);
   delay(k);
   ledOff(0,1,1);
  ledOn(0,1,0,0,0,1+i*2);
   delay(k);
   ledOff(0,1,0);
   
  ledOn(1,0,0,1+i*2,0,0);
  delay(k);
  ledOff(1,0,0);
  ledOn(1,0,1,1+i*2,0,0);
   delay(k);
   ledOff(1,0,1);   
  ledOn(1,1,1,1+i*2,0,0);
   delay(k);
   ledOff(1,1,1);
  ledOn(1,1,0,1+i*2,0,0);
   delay(k);
   ledOff(1,1,0);  
  }
}

void showSides(){
int i,j,z,k;
for(z=0;z<=5;z++){
  k=201-z*20;
  clearLeds();
  for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(0,i,j,15,0,0);
    }
   delay(k);
   clearLeds();
   for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(i,j,1,0,15,0);
    }   
   delay(k);
   clearLeds();
    for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(1,i,j,0,0,15);
     }
   delay(k);
   clearLeds();
   for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(i,j,0,15,0,0);
    } 
    delay(k);

      clearLeds();
  for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(0,i,j,15,15,0);
    }
   delay(k);
   clearLeds();
   for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(i,j,1,15,0,15);
    }   
   delay(k);
   clearLeds();
    for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(1,i,j,0,15,15);
     }
   delay(k);
   clearLeds();
   for(i=0;i<=1;i++){
    for(j=0;j<=1;j++)
      ledOn(i,j,0,15,15,0);
    } 
    delay(k);
   }
}
