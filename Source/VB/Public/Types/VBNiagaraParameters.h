// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraSystem.h"

// Niagara 이펙트가 코드가 이름으로 넣는 값을 받는지 묻는 곳. 없는 이름에 값을 넣으면 Niagara 는 말없이 무시한다.
//   런타임(피 끄기 스위치)과 자산 검사(타격 왜곡 설정)가 같이 쓴다.
namespace VBNiagaraParameters
{
	// 이 이름의 float User 파라미터를 갖고 있나.
	inline bool HasUserFloat(const UNiagaraSystem& System, FName Parameter)
	{
		return System.GetExposedParameters().FindParameterOffset(FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(),
			Parameter)) != nullptr;
	}

	// 이 이름의 색(LinearColor) User 파라미터를 갖고 있나.
	inline bool HasUserColor(const UNiagaraSystem& System, FName Parameter)
	{
		return System.GetExposedParameters().FindParameterOffset(FNiagaraVariableBase(FNiagaraTypeDefinition::GetColorDef(),
			Parameter)) != nullptr;
	}
}
