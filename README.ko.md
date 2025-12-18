encfsy
======

🌐 **언어**: [English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [Русский](README.ru.md) | [العربية](README.ar.md) | [Deutsch](README.de.md)

---

## encfsy 소개
encfsy는 **Dokany**와 **Crypto++**를 백엔드로 사용하는 Windows용 EncFS 구현입니다.
**64비트 시스템 전용**입니다.

디렉토리 트리 구조를 그대로 유지하면서 파일 이름과 내용을 암호화합니다.
드라이브 전체를 암호화하는 것과 비교하여, Dropbox, Google Drive, rsync 또는 기타 원격 저장소와 암호화된 데이터를 동기화하는 데 이상적입니다. 파일이 종단 간 암호화된 상태로 유지되므로 저장소 관리자가 내용을 볼 수 없습니다.

## ⚠️ 양방향 클라우드 동기화에서는 `--paranoia`를 사용하지 마세요
`--paranoia` 모드(외부 IV 체이닝)를 켠 상태에서 동기화 도구가 파일 이름을 변경하면, 파일 이름을 원래대로 돌려도 **내용은 복호화 불가능한 쓰레기 데이터**가 됩니다.
- Dropbox/OneDrive/Google Drive 등 양방향 동기화에서는 **`--paranoia`를 끄십시오**.
- 꼭 써야 한다면 파일 이름이 변하지 않는 단방향 백업 용도로만 사용하세요.

## 보안 기능
encfsy는 안전한 비밀번호 관리를 위해 **Windows Credential Manager**를 사용합니다.

- 비밀번호는 **DPAPI** (Data Protection API)로 암호화되어 현재 사용자 계정에 연결됩니다
- GUI와 encfs.exe 간에 stdin으로 비밀번호를 전달할 필요가 없어 가로채기 위험을 제거합니다
- "비밀번호 기억" 옵션으로 다음 실행 시 자동 입력을 위해 비밀번호를 저장합니다
- 비밀번호는 **각 암호화 디렉토리(rootDir)별로 별도로 저장**됩니다

### 비밀번호 저장 위치
저장된 비밀번호는 제어판 → 자격 증명 관리자 → Windows 자격 증명에서 확인할 수 있습니다.
`EncFSy:c:\path\to\encrypted`와 같은 이름으로 표시됩니다.

## GUI 사용법
**encfsw.exe**를 사용하면 명령줄 없이 쉽게 볼륨을 마운트 및 언마운트할 수 있습니다.

1. 암호화 디렉토리(rootDir) 선택
2. 마운트할 드라이브 문자 선택
3. 비밀번호 입력 ("Remember Password" 체크하여 저장 가능)
4. "Mount" 클릭

"Show Advanced Options"를 통해 명령줄 버전과 동일한 상세 설정에 접근할 수 있습니다.

## 명령줄에서 Credential Manager 사용
GUI에서 "Remember Password"를 체크하고 마운트하면 비밀번호가 Windows Credential Manager에 저장됩니다.
이후 `--use-credential` 옵션을 사용하여 명령줄에서 비밀번호 입력 없이 마운트할 수 있습니다.

```bash
# 1. 먼저 GUI에서 "Remember Password"를 체크하고 마운트
#    → 비밀번호가 Credential Manager에 저장됨

# 2. 이후 명령줄에서 비밀번호 입력 없이 마운트 가능
encfs.exe C:\Data M: --use-credential
```

## 파일 이름 길이 제한
encfsy는 최신 *long-path* API를 사용하므로 전체 경로에 대한 기존 260자 **MAX_PATH** 제한이 **적용되지 않습니다**.

그러나 NTFS는 여전히 각 경로 구성 요소(폴더 또는 파일 이름)를 **255 UTF-16 문자**로 제한합니다.
암호화로 인해 이름이 약 30% 정도 길어지므로, 구성 요소별 제한 내에 유지하고 long-path를 지원하지 않는 도구와의 호환성을 위해 **각 파일 이름을 175자 이내**로 유지하세요.

## 사용법

```
Usage: encfs.exe [options] <rootDir> <mountPoint>

인수:
  rootDir      (예: C:\test)                암호화하여 마운트할 디렉토리
  mountPoint   (예: M: 또는 C:\mount\dokan)   마운트 위치 - M:\와 같은 드라이브 문자
                                              또는 빈 NTFS 폴더

옵션:
  -u <mountPoint>                              지정된 볼륨 언마운트
  -l                                           현재 마운트된 Dokan 볼륨 목록 표시
  -v                                           연결된 디버거로 디버그 출력 전송
  -s                                           stderr로 디버그 출력 전송
  -i <ms>              (기본값: 120000)        실행 중인 작업이 중단되고 볼륨이
                                               언마운트되기 전 타임아웃(밀리초)
  --use-credential                             Windows Credential Manager에서 비밀번호 읽기
                                               (비밀번호는 저장된 상태로 유지)
                                               참고: GUI에서 "Remember Password"를 체크하여
                                               먼저 비밀번호를 저장해야 합니다
  --use-credential-once                        Windows Credential Manager에서 비밀번호를 읽고
                                               읽은 후 삭제 (일회용)
  --dokan-debug                                Dokan 디버그 출력 활성화
  --dokan-network <UNC>                        네트워크 볼륨의 UNC 경로 (예: \\host\myfs)
  --dokan-removable                            이동식 미디어로 볼륨 표시
  --dokan-write-protect                        읽기 전용으로 파일 시스템 마운트
  --dokan-mount-manager                        Windows Mount Manager에 볼륨 등록
                                               (휴지통 지원 등 활성화)
  --dokan-current-session                      현재 세션에서만 볼륨 표시
  --dokan-filelock-user-mode                   사용자 모드에서 LockFile/UnlockFile 처리;
                                               그렇지 않으면 Dokan이 자동으로 관리
  --dokan-enable-unmount-network-drive         탐색기에서 네트워크 드라이브 언마운트 허용
  --dokan-dispatch-driver-logs                 커널 드라이버 로그를 사용자 영역으로 전달 (느림)
  --dokan-allow-ipc-batching                   느린 파일 시스템(예: 원격 저장소)을 위한
                                               IPC 배칭 활성화
  --public                                     CreateFile에서 핸들을 열 때 호출 사용자를 가장
                                               관리자 권한 필요
  --allocation-unit-size <bytes>               볼륨에서 보고하는 할당 단위 크기
  --sector-size <bytes>                        볼륨에서 보고하는 섹터 크기
  --volume-name <name>                         탐색기에 표시되는 볼륨 이름 (기본값: EncFS)
  --volume-serial <hex>                        16진수 볼륨 일련 번호 (기본값: 하위 항목에서 가져옴)
  --paranoia                                   AES-256 암호화, 이름 변경 IV 및 외부 IV 체이닝 활성화
  --alt-stream                                 NTFS 대체 데이터 스트림 활성화
  --case-insensitive                           대소문자를 구분하지 않는 파일 이름 매칭 수행
  --reverse                                    역방향 모드: 평문 rootDir을 mountPoint에
                                               암호화하여 표시

예:
  encfs.exe C:\Users M:                                    # C:\Users를 M:\로 마운트
  encfs.exe C:\Users C:\mount\dokan                        # NTFS 폴더 C:\mount\dokan에 마운트
  encfs.exe C:\Users M: --dokan-network \\myfs\share       # UNC \\myfs\share로 네트워크 드라이브 마운트
  encfs.exe C:\Data M: --volume-name "보안 드라이브"        # 사용자 정의 볼륨 이름으로 마운트
  encfs.exe C:\Data M: --use-credential                    # Credential Manager에서 저장된 비밀번호 사용

언마운트하려면 이 콘솔에서 Ctrl+C를 누르거나 다음을 실행하세요:
  encfs.exe -u <mountPoint>
```

## 설치
1. **Dokany** (≥ 2.0) 설치 — [공식 릴리스](https://github.com/dokan-dev/dokany/releases)에서 다운로드
2. [Releases 페이지](https://github.com/mimidesunya/encfsy/releases)에서 최신 **encfsy 설치 프로그램**을 다운로드하고 설치 마법사를 따르세요

## 라이선스
[LGPL-3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)

## 저자
[Mimi](https://github.com/mimidesunya) ｜ [X @mimidesunya](https://twitter.com/mimidesunya)
