#!/usr/bin/env bash
# 헤드리스 프로브 로그 문자열을 판정 계약 형태로 뽑는다 (게이트 4).
#
# dedi-verify.ps1 은 이 문자열들을 정규식으로 grep 해 PASS/FAIL 을 낸다.
# 카테고리(LogTemp → LogRE*)는 R-02 에서 바뀌지만 **메시지 본문은 불변**이어야 한다.
# 그래서 카테고리 접두어와 실행마다 달라지는 수치는 지우고 본문 골격만 남긴다.
#
# 사용: extract-probe-strings.sh <server.log|client.log>
grep -oE "\[(Move|Attack|Dash|RE|GAS|UI|Facing|Stats)\] .*" "$1" \
  | sed -E 's/-?[0-9]+\.[0-9]+/#/g; s/\b[0-9]+\b/#/g' \
  | sed -E 's/X=# Y=# Z=#/VEC/g' \
  | sort | uniq -c | sed -E 's/^ +//'
