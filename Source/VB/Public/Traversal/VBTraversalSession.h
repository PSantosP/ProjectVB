// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // EMovementMode
#include "VBTraversalSession.generated.h"

class AVBCharacter;
class UPrimitiveComponent;

// 트래버설 세션 한 번이 캐릭터에서 빌린 것(무기, 이동 모드, 장애물 충돌)을 기록하고 돌려준다.
// 세션 단계, 면, 상태 태그, 틱은 UVBTraversalComponent 가 맡고 모드별 정리는 각 모드가 맡는다.
USTRUCT()
struct VB_API FVBTraversalSession
{
	GENERATED_BODY()

	// 맨틀류 진입. 무기를 임시로 치우고 Flying 으로 바꾸고 장애물 충돌을 끈다.
	void EnterMove(AVBCharacter& Character, UPrimitiveComponent* Obstacle, EMovementMode RecoverMode);
	// 면 진입. 맨틀류와 같고 속도를 0 으로 둔다. 이후 캡슐 자리는 모드가 매 틱 놓는다.
	void EnterSurface(AVBCharacter& Character, UPrimitiveComponent* Obstacle);

	void SetRecoverMode(EMovementMode Mode) { RecoverMovementMode = Mode; }
	EMovementMode GetRecoverMode() const { return RecoverMovementMode; }
	UPrimitiveComponent* GetIgnoredObstacle() const { return IgnoredObstacle; }

	// 무기, 이동 모드, 루트모션 배율, 장애물 충돌을 되돌린다. Character 가 null 이면 기록만 지운다.
	void Restore(AVBCharacter* Character);

private:
	void IgnoreObstacle(AVBCharacter& Character, UPrimitiveComponent* Obstacle);

	// 세션 중 충돌을 끈 장애물. 복구 때 다시 켠다.
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> IgnoredObstacle;

	EMovementMode RecoverMovementMode = MOVE_Walking;
};
