8B 95 00 FF FF FF 68 5C 4A 62 00 52 E8 88 CF E4 FF 83 C4 08 85 C0 0F 85 ED 29 E4 FF 8B 85 00 FF FF FF 68 9F F2 60 00 50 E8 6C CF E4 FF 83 C4 08 85 C0 74 47 8B 85 04 FF FF FF 85 C0 74 3D 8A 10 80 FA 30 7C 36 80 FA 39 7F 31 50 E8 AA CF FD FF 83 C4 04 83 F8 01 7C 07 83 F8 32 7F 09 EB 0C B8 01 00 00 00 EB 05 B8 32 00 00 00 A3 83 F2 60 00 66 A3 46 72 7C 00 E9 6F 24 E4 FF 8B 85 00 FF FF FF 68 B3 F2 60 00 50 E8 0D CF E4 FF 83 C4 08 85 C0 74 4C 8B 85 04 FF FF FF 85 C0 74 42 8A 10 80 FA 30 7C 3B 80 FA 39 7F 36 50 E8 4B CF FD FF 83 C4 04 83 F8 01 7C 07 83 F8 32 7F 09 EB 0C B8 01 00 00 00 EB 05 B8 32 00 00 00 A3 87 F2 60 00 66 A3 44 72 7C 00 A3 A0 21 9F 00 E9 0B 24 E4 FF 8B 85 00 FF FF FF 68 C7 F2 60 00 50 E8 A9 CE E4 FF 83 C4 08 85 C0 74 41 8B 85 04 FF FF FF 85 C0 74 37 8A 10 80 FA 30 7C 30 80 FA 39 7F 2B 50 E8 E7 CE FD FF 83 C4 04 83 F8 01 7C 07 83 F8 40 7F 09 EB 0C B8 01 00 00 00 EB 05 B8 40 00 00 00 A3 8B F2 60 00 E9 B2 23 E4 FF 8B 85 00 FF FF FF 68 E0 F2 60 00 50 E8 50 CE E4 FF 83 C4 08 85 C0 74 45 8B 85 04 FF FF FF 85 C0 74 3B 8A 10 80 FA 30 7C 34 80 FA 39 7F 2F 50 E8 8E CE FD FF 83 C4 04 3D A2 03 00 00 7C 09 3D 10 1D 00 00 7F 09 EB 0C B8 A2 03 00 00 EB 05 B8 10 1D 00 00 A3 8F F2 60 00 E9 55 23 E4 FF 8B 85 00 FF FF FF 68 FF F2 60 00 50 E8 F3 CD E4 FF 83 C4 08 85 C0 74 45 8B 85 04 FF FF FF 85 C0 74 3B 8A 10 80 FA 30 7C 34 80 FA 39 7F 2F 50 E8 31 CE FD FF 83 C4 04 83 F8 3E 90 90 7C 09 3D 6C 02 00 00 7F 09 EB 0C B8 3E 00 00 00 EB 05 B8 6C 02 00 00 A3 93 F2 60 00 E9 F8 22 E4 FF 8B 85 00 FF FF FF 68 19 F3 60 00 50 E8 96 CD E4 FF 83 C4 08 85 C0 74 45 8B 85 04 FF FF FF 85 C0 74 3B 8A 10 80 FA 30 7C 34 80 FA 39 7F 2F 50 E8 D4 CD FD FF 83 C4 04 83 F8 01 90 90 7C 09 83 F8 20 90 90 7F 09 EB 0C B8 01 00 00 00 EB 05 B8 20 00 00 00 A3 97 F2 60 00 E9 9B 22 E4 FF 8B 85 00 FF FF FF 68 2E F3 60 00 50 E8 39 CD E4 FF 83 C4 08 85 C0 74 40 8B 85 04 FF FF FF 85 C0 74 36 8A 10 80 FA 30 7C 2F 80 FA 39 7F 2A 50 E8 77 CD FD FF 83 C4 04 83 F8 01 90 90 7C 09 83 F8 20 90 90 7F 09 EB 0C B8 01 00 00 00 EB 05 B8 20 00 00 00 A3 9B F2 60 00 E9 3E 22 E4 FF

	WHOIS COMMAND [005E4C77]
	MOV EDX,[EBP-00000100] ; Pointer to String
	PUSH 00624A5C ; Get String
	PUSH EDX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JNE 00427680 ; Go Back To Console
	
	Client Wait Ticks [005E4C93]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F29F ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JE 005E4CF2 ; Skip Command
	MOV EAX,[EBP-0FC]  
	TEST EAX,EAX
	JE 005E4CF2 ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4CF2 ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4CF2 ; Skip Command
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0x1 ; Minimum Value
	JL 005E4CD6 ; Go to Min Value
	CMP EAX,0x32 ; Maximum Value
	JG 005E4CDD ; Go to Max Value
	JMP 005E4CE2 ; Go to Store Value
	MOV EAX,0x1 ; Set Minimum Value
	JMP 005E4CE2 ; Go to Store Value
	MOV EAX,0x32 ; Set Maximum Value
	MOV [0060F283],EAX ; Store Value
	MOV [007C7246],AX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Server Wait Ticks [005E4CF2]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F2B3 ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JE 005E4D56 ; Skip Command
	MOV EAX,[EBP-000000FC]
	TEST EAX,EAX
	JE 005E4D56 ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4D56 ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4D56 ; Skip Command
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0x1 ; Minimum Value
	JL 005E4D35 ; Go to Min Value
	CMP EAX,0x32 ; Maximum Value
	JG 005E4D3C ; Go to Max Value
	JMP 005E4D41 ; Go to Store Value
	MOV EAX,0x1 ; Set Minimum Value
	JMP 005E4D41 ; Go to Store Value
	MOV EAX,0x32 ; Set Maximum Value
	MOV [0060F287],EAX ; Store Value
	MOV [007C7244],AX ; Store Value
	MOV [009F21A0],EAX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Entity Updates Per Tick [005E4D56]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F2C7 ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,8
	TEST EAX,EAX
	JE 005E4DAF ; Skip Command
	MOV EAX,[EBP-0FC]
	TEST EAX,EAX
	JE 005E4DAF ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4DAF ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4DAF ; Skip Command	
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0x1 ; Minimum Value
	JL 005E4D99 ; Go to Min Value
	CMP EAX,0x40 ; Maximum Value
	JG 005E4DA0 ; Go to Max Value
	JMP 005E4DA5 ; Go to Store Value
	MOV EAX,0x1 ; Set Minimum Value
	JMP 005E4DA5 ; Go to Store Value
	MOV EAX,0x40 ; Set Maximum Value
	MOV [0060F28B],EAX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Master Server Update Rate [005E4DAF]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F2E0 ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JE 005E4E0C ; Skip Command
	MOV EAX,[EBP-0FC]
	TEST EAX,EAX
	JE 005E4E0C ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4E0C ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4E0C ; Skip Command	
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0X3A2 ; Minimum Value
	JL 005E4DF6 ; Go to Min Value
	CMP EAX,0X1D10 ; Maximum Value
	JG 005E4DFD ; Go to Max Value
	JMP 005E4E02 ; Go to Store Value
	MOV EAX,0X3A2 ; Set Minimum Value
	JMP 005E4E02 ; Go to Store Value
	MOV EAX,0X1D10 ; Set Maximum Value
	MOV [0060F28F],EAX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Player Stat Update Rate [005E4E0C]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F2FF ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,8
	TEST EAX,EAX
	JE 005E4E69 ; Skip Command
	MOV EAX,[EBP-0FC]
	TEST EAX,EAX
	JE 005E4E69 ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4E69 ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4E69 ; Skip Command
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0X3E ; Minimum Value
	JL 005E4E53 ; Go to Min Value
	CMP EAX,0X26C ; Maximum Value
	JG 005E4E5A ; Go to Max Value
	JMP 005E4E5F ; Go to Store Value
	MOV EAX,0x3E ; Set Minimum Value
	JMP 005E4E5F ; Go to Store Value
	MOV EAX,0x26C ; Set Maximum Value
	MOV [0060F293],EAX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Claymore World Limit [005E4E69]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F319 ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JE 005E48DC ; Skip Command
	MOV EAX,[EBP-000000FC]
	TEST EAX,EAX
	JE 005E4EC6 ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4E69 ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4E69 ; Skip Command
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0x1 ; Minimum Value
	JL 005E4EB0 ; Go to Min Value
	CMP EAX,0x20 ; Maximum Value
	JG 005E4EB7 ; Go to Max Value
	JMP 005E4EBC ; Go to Store Value
	MOV EAX,0x1 ; Set Minimum Value
	JMP 005E48D3 ; Go to Store Value
	MOV EAX,0x20 ; Set Maximum Value
	MOV [0060F297],EAX ; Store Value
	JMP 00427161 ; Go Back To Console
	
	Satchel Charge World Limit [005E4EC6]
	MOV EAX,[EBP-00000100] ; Pointer to String
	PUSH 0060F32E ; Get String
	PUSH EAX ; Param Pointer To String
	CALL 00431C10 ; Compare Input with String
	ADD ESP,08
	TEST EAX,EAX
	JE 005E4F1E ; Skip Command
	MOV EAX,[EBP-000000FC]
	TEST EAX,EAX
	JE 005E4F1E ; Skip Command
	MOV DL,[EAX]
	CMP DL,'0' ; Check Value
	JL 005E4F1E ; Skip Command
	CMP DL,'9' ; Check Value
	JG 005E4F1E ; Skip Command
	PUSH EAX
	CALL 005C1C71 ; Parse Number from String
	ADD ESP,4
	CMP EAX,0x1 ; Minimum Value
	JL 005E4F0D ; Go to Min Value
	CMP EAX,0x20 ; Maximum Value
	JG 005E4F14 ; Go to Max Value
	JMP 005E4F19 ; Go to Store Value
	MOV EAX,0x1 ; Set Minimum Value
	JMP 005E4F19 ; Go to Store Value
	MOV EAX,0x20 ; Set Maximum Value
	MOV [0060F29B],EAX ; Store Value
	JMP 00427161 ; Go Back To Console