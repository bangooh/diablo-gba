on run arguments
    set builderPath to item 1 of arguments
    set appTitle to "Diablo GBA ROM Builder · 0.9.5 EZ-Flash IV"
    try
        activate
        set languageChoice to button returned of (display dialog "Diablo GBA ROM Builder for macOS" & return & return & "ROM: V0.9.5 · 32 MiB" & return & "Unofficial fan project. Not affiliated with Nintendo or Blizzard." & return & "Select your language / 언어 선택" with title appTitle buttons {"Cancel", "English", "한국어"} default button "한국어" cancel button "Cancel")
        set korean to languageChoice is "한국어"
        if korean then
            set mpqPrompt to "DIABDAT.MPQ 선택: Diablo 1.0 (517,501,242바이트) 또는 GOG (517,501,282바이트)."
            set savePrompt to "새 GBA 롬의 저장 위치와 이름을 선택하세요. 기존 파일은 덮어쓰지 않습니다."
            set readyText to "지원되는 MPQ를 확인한 뒤 내장 데이터를 복원합니다." & return & "생성 롬: V0.9.5 · 32 MiB" & return & return & "변환 시작 후 완료 창이 나타날 때까지 기다려 주세요."
            set buildButton to "롬 만들기"
            set cancelButton to "취소"
            set showButton to "Finder에서 보기"
            set doneButton to "완료"
            set doneText to "롬 생성 완료!" & return & "SHA-256 검증을 통과했습니다."
        else
            set mpqPrompt to "Select DIABDAT.MPQ: Diablo 1.0 (517,501,242 bytes) or GOG (517,501,282 bytes)."
            set savePrompt to "Choose a NEW .gba filename. Existing files are never overwritten."
            set readyText to "Your MPQ will be verified, then the bundled ROM data will be restored." & return & "Output: V0.9.5 · 32 MiB" & return & return & "Please wait for the completion dialog after starting."
            set buildButton to "Create ROM"
            set cancelButton to "Cancel"
            set showButton to "Show in Finder"
            set doneButton to "Done"
            set doneText to "ROM created!" & return & "SHA-256 verification passed."
        end if
        if korean then
            set cartridgeNote to "EZ-Flash IV 전용: 128KB 뱅크 전환 SRAM 저장. NOR에서 실행하세요." & return & "기존 SAV를 백업하고 DIABLO.gba / DIABLO.sav로 파일명을 맞추세요."
        else
            set cartridgeNote to "EZ-Flash IV only: 128KB banked-SRAM save. Run this ROM from NOR." & return & "Back up your SAV; use matching DIABLO.gba / DIABLO.sav filenames."
        end if
        set readyText to readyText & return & return & cartridgeNote
        set doneText to doneText & return & return & cartridgeNote
        set mpqPath to POSIX path of (choose file with prompt mpqPrompt)
        set outputPath to POSIX path of (choose file name with prompt savePrompt default name "DIABLO.gba")
        display dialog readyText & return & return & outputPath with title appTitle buttons {cancelButton, buildButton} default button buildButton cancel button cancelButton
        try
            with timeout of 3600 seconds
                do shell script (quoted form of builderPath) & " --build " & (quoted form of mpqPath) & " " & (quoted form of outputPath)
            end timeout
        on error errorMessage number errorNumber
            if korean then
                set explanation to "롬을 만들지 못했습니다. " & return & "지원되는 원본 MPQ인지, 저장 이름이 .gba로 끝나는지, 같은 이름의 파일이 없는지 확인해 주세요."
                if errorNumber is 9 then set explanation to "같은 이름의 파일이 이미 있습니다. 다른 이름으로 저장해 주세요."
                if errorNumber is 4 then set explanation to "DIABDAT.MPQ를 읽을 수 없습니다. 지원 판본: Diablo 1.0 (517,501,242바이트) 또는 GOG (517,501,282바이트)."
                if errorNumber is 5 then set explanation to "이 MPQ 버전은 지원되지 않거나 앱의 데이터가 손상되었습니다. 롬은 저장하지 않았습니다."
                if errorNumber is 10 then set explanation to "저장할 파일 이름은 .gba로 끝나야 합니다."
                if errorNumber is 7 then set explanation to "저장할 수 없습니다. 쓰기 가능한 맥 내부 폴더와 40MB 이상의 여유 공간을 확인하세요."
            else
                set explanation to "ROM creation failed. Check the original MPQ, use a new .gba filename and a writable local folder."
            end if
            set lastLine to last paragraph of errorMessage
            display dialog explanation & return & return & lastLine with title appTitle buttons {"OK"} default button "OK" with icon caution
            return
        end try
        activate
        set resultButton to button returned of (display dialog doneText & return & return & outputPath with title appTitle buttons {doneButton, showButton} default button showButton)
        if resultButton is showButton then do shell script "/usr/bin/open -R " & (quoted form of outputPath)
    on error errorMessage number errorNumber
        if errorNumber is not -128 then display dialog errorMessage with title appTitle buttons {"OK"} default button "OK" with icon caution
    end try
end run
