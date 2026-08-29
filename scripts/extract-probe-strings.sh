#!/usr/bin/env bash
# 헤드리스 프로브 로그 문자열을 판정 계약 형태로 뽑는다 (게이트 4).
#
# dedi-verify.ps1 이 이 문자열들을 정규식으로 grep 해 PASS/FAIL 을 낸다.
# 카테고리(LogTemp → LogRE*)는 R-02 에서 바뀌지만 **메시지 본문은 불변**이어야 한다.
# 그래서 카테고리 접두어와 수치를 지우고 본문 골격만 남긴다.
#
# **프로브 로그([Move]/[Attack]/[Dash])만 본다.** [RE] 게임루프 로그는 같은 실행에서도
# 보스 패턴 로테이션(FMath::Rand 시드)과 사망 타이밍에 따라 종류가 달라져 diff 가 성립하지
# 않는다. 프로브는 고정 타이머 시퀀스라 결정론이고, 그게 이 게이트가 지키려는 계약이다.
#
# 사용: extract-probe-strings.sh <server.log|client.log>
grep -oE "\[(Move|Attack|Dash)\] .*" "$1" \
  | sed -E 's/-?[0-9]+\.[0-9]+/#/g; s/\b[0-9]+\b/#/g' \
  | sed -E 's/X=# Y=# Z=#/VEC/g' \
  | sort -u
