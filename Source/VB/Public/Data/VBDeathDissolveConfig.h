// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBDeathDissolveConfig.generated.h"

class UMaterialInterface;
class UNiagaraSystem;

/**
 * 래그돌로 누운 시체가 한쪽부터 깎여 가루로 사라지는 연출의 수치 묶음. 소비자는 UVBDeathDissolveComponent.
 * Duration 이 UVBEnemyConfig::DeathCleanupDelay 안에 드는지는 UVBEnemyConfig::IsDataValid 가 검사한다.
 */
UCLASS(BlueprintType)
class VB_API UVBDeathDissolveConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 깎여 사라지는 데 걸리는 시간(실초). 0 이하 = 이 연출을 쓰지 않는다.
	// 실초인 이유: 머티리얼이 View.RealTime 을 읽어 히트스톱 중에도 화면상 길이가 같아야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0"))
	float Duration = 1.2f;

	// 깎이는 경계의 발광색.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FLinearColor EdgeColor = FLinearColor(1.0f, 0.45f, 0.18f, 1.0f);

	// 경계 두께(마스크 기준 0~1).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EdgeWidth = 0.08f;

	// 경계 발광 세기(Emissive 배수). 1 을 초과해야 블룸이 붙는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0"))
	float EdgeGlow = 6.0f;

	// 깎여 나가는 방향(오브젝트 로컬). 월드 축이면 여러 시체가 같은 높이에서 동시에 깎인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FVector AxisLocal = FVector(0.0f, 0.0f, 1.0f);

	// 노이즈 밀도. 작으면 큰 덩어리, 크면 고운 가루로 부서진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0"))
	float NoiseScale = 4.0f;

	// 경계 근처 정점을 축 방향으로 미는 거리(cm). 0 이면 제자리에서 지워진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0"))
	float Push = 12.0f;

	// 날아가는 파편, 먼지. 비면 디졸브만 돈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<UNiagaraSystem> DebrisEffect;

	// 죽은 적의 스켈레탈 메시 컴포넌트를 꽂을 파편 NS 의 User 파라미터 이름. 비우면 꽂지 않는다.
	// 이름이 NS 와 어긋나면 엔진이 조용히 무시하므로 런타임이 꽂은 사실을 로그로 남긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FName DebrisMeshParameterName = TEXT("SkeletalMesh");

	// 파편 재질. 런타임에 MID 를 만들어 그 몬스터의 텍스처를 꽂는다. 비면 Niagara 렌더러 재질 그대로.
	// Niagara 컴포넌트에는 재질 슬롯이 없어 자산이 들고 있는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<UMaterialInterface> DebrisMaterial;

	// 위 MID 를 넘길 파편 NS 의 재질 User 파라미터 이름.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FName DebrisMaterialParameterName = TEXT("DebrisMaterial");
};
