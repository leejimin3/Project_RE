#!/usr/bin/env bash
# 패턴 캡처 run.log 에서 게이트 6(스폰 수·페이즈 길이) 판정용 서명을 뽑는다.
#
# 실행마다 달라지는 것을 걷어내야 리팩터 전후 비교가 성립한다:
#   - 첫 페이즈는 -ExecCmds 가 도착하기 전이라 FMath::Rand() 로 뽑힌 **랜덤 패턴**이다
#     (REGameMode.cpp StartFiring). 고정 패턴의 첫 페이즈 로그부터 잘라 쓴다.
#   - Angle / Elapsed / 볼리 횟수는 시간·위치 함수라 제외한다.
# 남는 것: 페이즈 이름 + 길이, 볼리당 스폰 수(N), 곡사 Shape 와 체공(Flight).
#
# 사용: extract-pattern-signature.sh <패턴이름> <run.log>
Name="$1"; Log="$2"
MARKER="[RE] Boss Phase: ${Name} "
awk -v m="$MARKER" 'index($0, m) {found=1} found' "$Log" \
  | grep -oE "\[RE\] Boss (Phase: [A-Za-z]+ [0-9.]+s|FireDirect: Pattern=[0-9]+ Angle=[-0-9.]+ N=[0-9]+|FireArtillery: Pattern=[0-9]+ Shape=[0-9]+ N=[0-9]+ Flight=[0-9.]+)" \
  | sed -E 's/Angle=[-0-9.]+ //' \
  | sort -u
