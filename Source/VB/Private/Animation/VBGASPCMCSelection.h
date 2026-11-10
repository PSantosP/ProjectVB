#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
class UChooserTable;

namespace VBGASP
{
	// 네이티브 CMC 선택 한 번: 상태를 입력으로 쓰고 선택기를 평가해 결과를 재생 입력으로 낸다. 정의는 라이브러리의 _Selection 파일에 있다.
	void SelectNativeCMC(UAnimInstance* Instance, uint8 MachineState, bool bForceBlend, UChooserTable* Chooser);
}
