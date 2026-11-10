// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StructUtils/InstancedStruct.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponPickup.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 땅에 놓인 무기. GrantWeapon 결과로 획득을 한 곳에서 끝낸다: 새로 얻음 = 소진 후 AcquiredEffects, 이미 가짐 = 소진만, 실패 = 그대로.
 * 상호작용의 CompletionEffects 는 성공과 무관하게 돌아서 쓰지 않는다 - 그 칸은 이 액터에서 비워 둔다.
 * 재획득 방지는 켜짐 저장이, 가진 무기 목록 저장은 플레이어 빌드 저장이 맡는다.
 */
UCLASS()
class VB_API AVBWeaponPickup : public AActor
{
	GENERATED_BODY()

public:
	AVBWeaponPickup();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 집으면 가지는 무기.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapon")
	EVBWeaponType Weapon = EVBWeaponType::Blade;

	// 새로 얻었을 때만 적용한다(이미 가진 무기면 안 건다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Weapon", meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> AcquiredEffects;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	void ApplyPickedUp(bool bPickedUp);
	// 게임 월드에서 한 번. 실체화 재질 슬롯마다 동적 인스턴스를 만들어 메시 bounds 로 범위를 맞추고 진행 1(다 나타남)로 둔다.
	void ShowWeaponFormed();
};
