
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);      // I2C 주소, 열 16, 행 2

// ── 1. 몸통 스프라이트 (위쪽 줄부터, 한 줄 = 5픽셀) ───────────────
//    1 = 켜진 점, 0 = 꺼진 점 / 눈은 '꺼진 점'으로 표현
const byte SPR_NORMAL[]  = { B01110,   //  .###.   머리
                             B10101,   //  #.#.#   눈 두 개
                             B11111,   //  #####
                             B11111 }; //  #####   (4줄)

const byte SPR_BLINK[]   = { B01110,
                             B11111,   //  눈 감음
                             B11111,
                             B11111 };

const byte SPR_SQUASH[]  = { B01110,   //  살짝 찌그러짐 (3줄)
                             B10101,
                             B11111 };

const byte SPR_FLAT[]    = { B10101,   //  착지 순간 납작 (2줄)
                             B11111 };

const byte SPR_STRETCH[] = { B01110,   //  위로 길게 늘어남 (6줄)
                             B10101,
                             B11111,
                             B01110,
                             B01110,
                             B00100 };

// 높이(lift)에 따른 바닥 그림자 : 높이 올라갈수록 작아짐
const byte SHADOW[5] = { B00000, B01110, B01110, B00100, B00100 };

// ── 2. 동작 단계와 시간(ms) ─────────────────────────────────────
enum Phase { IDLE, CROUCH, LAUNCH, AIR, LAND, RECOVER };

const unsigned int T_IDLE    = 520;  // 서 있기
const unsigned int T_CROUCH  = 140;  // 웅크리기(점프 준비)
const unsigned int T_LAUNCH  =  70;  // 쭉 늘어나며 도약
const unsigned int T_AIR     = 640;  // 공중 체공
const unsigned int T_LAND    =  90;  // 납작 착지
const unsigned int T_RECOVER = 120;  // 탄성으로 복원
const byte         MAX_LIFT  =   4;  // 최고 높이(픽셀)

Phase         phase      = IDLE;
unsigned long phaseStart = 0;
byte          jumpCount  = 0;

byte frameBuf[8];          // 이번에 그릴 그림
byte lastFrame[8];         // 마지막으로 LCD에 보낸 그림

// ── 3. 프레임 합성 : 스프라이트를 lift만큼 띄워서 8줄 그림 만들기 ──
void compose(const byte *spr, byte h, byte lift) {
  memset(frameBuf, 0, 8);
  int bottom = 7 - lift;                 // 스프라이트 맨 아랫줄 위치
  int top    = bottom - h + 1;
  for (byte i = 0; i < h; i++) {
    int r = top + i;
    if (r >= 0 && r < 8) frameBuf[r] = spr[i];
  }
  if (lift > 0) frameBuf[7] |= SHADOW[lift];   // 공중이면 그림자
}

// 그림이 바뀌었을 때만 CGRAM 갱신 (불필요한 통신 줄이기)
void pushFrame() {
  if (memcmp(frameBuf, lastFrame, 8) == 0) return;
  lcd.createChar(0, frameBuf);   // 0번 슬롯 그림 교체 → (0,0) 글자 즉시 바뀜
  memcpy(lastFrame, frameBuf, 8);
}

void nextPhase(Phase p, unsigned long now) {
  phase = p;
  phaseStart = now;
}

// ── 4. 애니메이션 상태 기계 ────────────────────────────────────
void animate(unsigned long now) {
  unsigned long t = now - phaseStart;

  switch (phase) {
    case IDLE: {
      // 3번 점프마다 한 번, 서 있는 동안 눈 깜빡
      bool blink = (jumpCount % 3 == 0) && (t > 200 && t < 330);
      compose(blink ? SPR_BLINK : SPR_NORMAL, 4, 0);
      if (t >= T_IDLE) nextPhase(CROUCH, now);
      break;
    }
    case CROUCH:
      compose(SPR_SQUASH, 3, 0);
      if (t >= T_CROUCH) nextPhase(LAUNCH, now);
      break;

    case LAUNCH:
      compose(SPR_STRETCH, 6, 0);
      if (t >= T_LAUNCH) nextPhase(AIR, now);
      break;

    case AIR: {
      // 포물선 : 높이 = 최고높이 × 4p(1-p),  p = 경과시간 / 체공시간
      float p = (float)t / T_AIR;
      if (p > 1.0) p = 1.0;
      byte lift = (byte)(MAX_LIFT * 4.0 * p * (1.0 - p) + 0.5);
      // 바닥 근처(빠를 때)는 늘어난 모습, 높은 곳은 동그란 모습
      if (lift <= 1) compose(SPR_STRETCH, 6, lift);
      else           compose(SPR_NORMAL,  4, lift);
      if (t >= T_AIR) nextPhase(LAND, now);
      break;
    }
    case LAND:
      compose(SPR_FLAT, 2, 0);
      if (t >= T_LAND) nextPhase(RECOVER, now);
      break;

    case RECOVER:
      compose(SPR_SQUASH, 3, 0);
      if (t >= T_RECOVER) {
        jumpCount++;
        nextPhase(IDLE, now);
      }
      break;
  }
  pushFrame();
}

// ── 5. 둘째 줄 : 동작 시간 표시 (1초마다) ───────────────────────
void showUptime(unsigned long now) {
  static unsigned long lastSec = 0xFFFFFFFF;
  unsigned long sec = now / 1000;
  if (sec == lastSec) return;
  lastSec = sec;

  char buf[17];
  snprintf(buf, sizeof(buf), "UP %02lu:%02lu:%02lu",
           sec / 3600, (sec / 60) % 60, sec % 60);
  lcd.setCursor(0, 1);   // createChar 뒤에는 반드시 커서를 다시 지정
  lcd.print(buf);
}

void setup() {
  lcd.init();                        // I2C LCD 초기화
  lcd.backlight();                   // 백라이트 켜기
  memset(lastFrame, 0xFF, 8);        // 첫 프레임이 꼭 전송되도록
  compose(SPR_NORMAL, 4, 0);
  pushFrame();

  lcd.setCursor(0, 0);
  lcd.write(byte(0));                // (0,0)에 0번 사용자 정의 문자 배치
  lcd.setCursor(2, 0);
  lcd.print("RUNNING");

  phaseStart = millis();
}

void loop() {
  unsigned long now = millis();
  animate(now);      // 말랑이 움직이기
  showUptime(now);   // 동작 시간 갱신

}
