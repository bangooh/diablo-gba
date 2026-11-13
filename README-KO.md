# Diablo GBA 0.9.6 source distribution — English / Korean

## English

This is an unofficial fan project by Bangoo: a C-based GBA-specific engine
developed with reference to Devilution and DevilutionX. It is not an official
Blizzard or Nintendo product. Special thanks to DevilutionX and team Mupung.

The package includes game code, MPQ conversion tools and existing attributed
3×5, 5×5 and 7×7 font inputs. Game artwork, audio, videos, MPQs, ROMs and saves
are not included. The 7×7 font is stored in 8×8 cells. Existing font attribution
notices are preserved.

### Version 0.9.6

Thumb/O2 simulation optimization; existing offline CPU-party support retained.
See [changes and validation](docs/RELEASE-0.9.6.md).

### Building

Follow the [build instructions](docs/BUILDING.md) using your own DIABDAT.MPQ.
The reference environment is Linux, Python 3.12, GCC/G++, ffmpeg and Zig 0.13.0.

```sh
python -m pip install -r requirements-assets.txt
python scripts/regenerate_complete.py --mpq /private/DIABDAT.MPQ --output /private/new-diablo-build --zig /path/to/zig
```

The output must be a new directory outside the source tree. Do not add generated
assets or ROMs to the public repository. An existing generated directory is not
an input to this pipeline.

### Validation scope

Previous integration checks covered complete generation from a Diablo 1.0 MPQ,
Standard and EZ4 ROM builds, and Standard-ROM checks in mGBA: creating a fresh
hero, entering town and floor 1, saving 128KB, loading in a fresh run, and loading
floors 5 and 16.

An independent unattended clean build, GOG MPQ generation, physical GBA/EZ4
cartridge/link-cable tests remain unverified. These integration tests were not
rerun during repository cleanup.

Asset-free checks:

```sh
python scripts/check_public_tree.py
python scripts/verify_reference_sources.py
python scripts/test_core.py
```

### License

This project is non-commercial
and source-available; it is not presented as MIT-licensed or OSI-approved open
source. License scope and existing notices are in LICENSE.md, NOTICE.md and
docs/THIRD_PARTY.md. The source license grants no rights to game assets.
Public repository: https://github.com/bangooh/diablo-gba

---

## Korean / 한국어

### Diablo GBA 0.9.6 소스 배포본

Bangoo의 비공식 팬 프로젝트입니다. Devilution과 DevilutionX를 참고한
C 기반 GBA 전용 엔진이며 Blizzard 또는 Nintendo의 공식 제품이 아닙니다.
DevilutionX와 team Mupung에 특별히 감사드립니다.

게임 코드, MPQ 변환 도구, 출처가 표시된 기존 3×5·5×5·7×7 폰트 입력을
포함합니다. 게임 이미지·음원·영상·MPQ·ROM·세이브는 포함하지 않습니다.
7×7 폰트는 8×8 셀에 저장됩니다. 폰트의 기존 출처 고지는 유지합니다.

### 버전 0.9.6

시뮬레이션 코드의 Thumb/O2 최적화를 적용했습니다. 혼자 클라이언트로
인식돼도 R로 CPU 파티를 시작하는 기존 수정은 유지합니다.
측정 결과와 검증 범위는 [변경 기록](docs/RELEASE-0.9.6.md)을 참고하세요.

### 빌드

[빌드 안내](docs/BUILDING.md)에 따라 본인이 보유한 DIABDAT.MPQ를 사용합니다.
Linux, Python 3.12, GCC/G++, ffmpeg, Zig 0.13.0이 기준 환경입니다.

```sh
python -m pip install -r requirements-assets.txt
python scripts/regenerate_complete.py --mpq /private/DIABDAT.MPQ --output /private/new-diablo-build --zig /path/to/zig
```

출력 경로는 소스 폴더 밖의 새 폴더여야 합니다. 생성된 자산과 ROM은
공개 저장소에 추가하지 마세요. 기존 generated 폴더는 이 경로의 입력이 아닙니다.

### 검증 범위

이전 통합 검증에서 Diablo 1.0 MPQ 전체 생성과 일반용·EZ4 ROM 빌드,
일반용 mGBA 새 캐릭터 생성·마을·1층 진입·128KB 저장·새 실행 불러오기,
5층·16층 로드를 확인했습니다.

독립 환경에서의 무인 클린 빌드, GOG MPQ, 실기·EZ4 카트리지·링크 케이블은
아직 검증되지 않았습니다. 이번 저장소 정리에서 통합 검증을 다시 실행하지는 않았습니다.

자산 없는 검사는 다음과 같습니다.

```sh
python scripts/check_public_tree.py
python scripts/verify_reference_sources.py
python scripts/test_core.py
```

### 라이선스

프로젝트는 비상업적 source-available 형태이며 MIT 또는 OSI 오픈소스로
표시하지 않습니다. 적용 범위와 기존 고지는 LICENSE.md, NOTICE.md,
docs/THIRD_PARTY.md에 있습니다. 소스 라이선스는 게임 자산의 권리를
부여하지 않습니다. 공개 저장소: https://github.com/bangooh/diablo-gba
