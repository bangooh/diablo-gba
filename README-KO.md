# Diablo GBA 0.9.5 소스 배포본

Bangoo의 비공식 팬 프로젝트입니다. Devilution과 DevilutionX를 참고한
C 기반 GBA 전용 엔진이며 Blizzard 또는 Nintendo의 공식 제품이 아닙니다.
DevilutionX와 team Mupung에 특별히 감사드립니다.

게임 코드, MPQ 변환 도구, 출처가 표시된 기존 3×5·5×5·7×7 폰트 입력을
포함합니다. 게임 이미지·음원·영상·MPQ·ROM·세이브는 포함하지 않습니다.
7×7 폰트는 8×8 셀에 저장됩니다. 폰트의 기존 출처 고지는 유지합니다.

## 빌드

[빌드 안내](docs/BUILDING.md)에 따라 본인이 보유한 DIABDAT.MPQ를 사용합니다.
Linux, Python 3.12, GCC/G++, ffmpeg, Zig 0.13.0이 기준 환경입니다.

```sh
python -m pip install -r requirements-assets.txt
python scripts/regenerate_complete.py --mpq /private/DIABDAT.MPQ --output /private/new-diablo-build --zig /path/to/zig
```

출력 경로는 소스 폴더 밖의 새 폴더여야 합니다. 생성된 자산과 ROM은
공개 저장소에 추가하지 마세요. 기존 generated 폴더는 이 경로의 입력이 아닙니다.

## 검증 범위

저장된 통합 검증 기록에서 Diablo 1.0 MPQ 전체 생성과 일반용·EZ4 ROM 빌드,
일반용 mGBA 새 캐릭터 생성·마을·1층 진입·128KB 저장·새 실행 불러오기,
5층·16층 로드를 확인했습니다. 엔딩 정지화면은 MPQ 영상에서 추출한 것으로
변경되어 이전 배포 ROM과 해시가 다릅니다.

독립 환경에서의 무인 클린 빌드, GOG MPQ, 실기·EZ4 카트리지·링크 케이블,
Windows/macOS GUI 컨버터 검증은 별도 남은 항목입니다.
전체 생성 및 실행 검증을 이번 패키징 작업에서 새로 수행했다는 뜻은 아닙니다.

자산 없는 검사는 다음과 같습니다.

```sh
python scripts/check_public_tree.py
python scripts/verify_reference_sources.py
python scripts/test_core.py
```

## 공개 안내

[GitHub 업로드 안내](docs/GITHUB_UPLOAD.md)와
[현재 상태](docs/PUBLICATION_STATUS.md)를 참고하세요.
프로젝트는 비상업적 source-available 형태이며 MIT 또는 OSI 오픈소스로
표시하지 않습니다. 적용 범위와 기존 고지는 LICENSE.md, NOTICE.md,
docs/THIRD_PARTY.md에 있습니다. 소스 라이선스는 게임 자산의 권리를
부여하지 않습니다. 공개 저장소: https://github.com/bangooh/diablo-gba
