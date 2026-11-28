# CrashDoc — 원격 크래시 분석 시스템

ELEC462 System Programming, Team 23 (김경민, 최완호, 추진성)

프로그램을 대신 실행하다가 비정상 종료하면, 커널이 알려주는 종료 정보와 실행 시간·최대 메모리·마지막 출력을 모아
TCP로 분석 서버에 보내고, 원인 추정과 점검 포인트를 받아 보여줍니다.

## 빌드

```
make            # crashdoc, crashdoc-server
make tests      # tests/ 의 크래시 테스트 프로그램
make clean
```

환경: Ubuntu 24.04, gcc

## 실행

```
# 터미널 1: 서버
./crashdoc-server 9000

# 터미널 2: 클라이언트
./crashdoc run ./tests/segv
./crashdoc build gcc main.c -o main
./crashdoc history
./crashdoc show 12
```

## 구조

| 폴더 | 내용 | 담당 |
| --- | --- | --- |
| `common/` | 메시지 송수신, 공통 구조체 | 추진성 |
| `client/` | crashdoc 클라이언트 | 최완호 |
| `server/` | 서버 코어, 대시보드 | 추진성 |
| `server/analyzer/` | 분석기 (규칙 기반 + LLM) | 김경민 |
| `server/store/` | 크래시 기록 | 김경민 |
| `tests/` | 일부러 죽는 테스트 프로그램 | 최완호 |