#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
class UChooserTable;

namespace VBGASP
{
	// 네이티브 CMC 선택기가 없는 그래프의 선택: 문맥 둘의 선택기를 평가해 옛 BlendStackInputs 를 채운다.
	void SelectLegacyBlendInputs(UAnimInstance* Instance, uint8 StateMachineState, bool bForceBlend, UChooserTable* Chooser);
}
