#pragma once

#include "CoreMinimal.h"

class AActor;
class USkeletalMeshComponent;
class UVBEnemyConfig;
class UVBDeathDissolveComponent;
#if WITH_EDITOR
class FDataValidationContext;
#endif

// 세션, 설정을 갖지 않고 저작 동작을 기존 dissolve 실행기로 전달한다.
// 에디터 진입점은 적 액터 쪽에 있고, config 와 메시 갱신 정책은 각 담당 클래스가 가진다.
namespace VBEnemyAuthoring
{
	VB_API void PreviewDeathDissolve(AActor& Owner, const UVBEnemyConfig& Config, UVBDeathDissolveComponent* DissolveComponent);
	VB_API void ResetDeathDissolvePreview(UVBDeathDissolveComponent* DissolveComponent);
#if WITH_EDITOR
	// 실패하면 기존 guard 오류를 더한다. Super 의 NotValidated 를 Valid 로 바꾸지 않는다.
	VB_API bool ValidateAnimation(USkeletalMeshComponent* Mesh, const UVBEnemyConfig& Config, FDataValidationContext& Context);
#endif
}
