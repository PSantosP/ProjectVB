// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/VBStationSessionComponent.h"
#include "Inventory/Crafting/VBCraftingRules.h"
#include "Inventory/Enhancement/VBEnhancementRules.h"
#include "VBCraftingComponent.generated.h"

class UVBCraftingStationDefinition;
class UVBEnhancementComponent;
class UVBInventoryComponent;
class UVBItemDefinition;
struct FVBCraftRecipe;

/** 보기의 한 줄. 조건은 서버가 판정한 것 그대로다. */
USTRUCT()
struct VB_API FVBCraftingViewEntry
{
	GENERATED_BODY()

	// 제작대 정의의 Recipes 안 번호.
	UPROPERTY()
	int32 RecipeIndex = INDEX_NONE;

	// 조건. 거짓인데 보이는 줄은 bShowWhenUnavailable 인 것뿐이다.
	UPROPERTY()
	bool bAvailable = true;
};

/** 소유 클라이언트가 그릴 현재 제작대. 열려 있지 않으면 IsActive() 가 거짓이다. */
USTRUCT()
struct VB_API FVBCraftingView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBCraftingStationDefinition> Station = nullptr;

	UPROPERTY()
	TArray<FVBCraftingViewEntry> Entries;

	bool IsActive() const { return Station != nullptr; }
	const FVBCraftRecipe* GetRecipe(const FVBCraftingViewEntry& ViewEntry) const;
};

/**
 * 제작, 강화 진행을 관리한다(AVBPlayerController 에 붙음). 서버가 열린 제작대를 보유하고, 클라이언트는 뷰를 그리며 요청만 보낸다.
 * 서버는 모든 행동을 다시 검사한 뒤 교환 한 번(TryExchange)으로 바꾼다. 강화 단계는 교환 뒤에만 쓴다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBCraftingComponent : public UVBStationSessionComponent
{
	GENERATED_BODY()

public:
	UVBCraftingComponent();

	// 서버에서만. 이미 열려 있거나 돈 아이템이 없으면 false. Counterpart 는 대장장이, 작업대(사라지면 닫힌다) - 없어도 된다.
	bool OpenStation(const UVBCraftingStationDefinition* Station, AActor* Counterpart);

	// 서버에서만. 열려 있지 않으면 아무것도 안 한다.
	void CloseStation();

	bool IsStationOpen() const { return IsSessionOpen(); }

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestCraft(int32 RecipeIndex);
	void RequestEnhance(const FVBEnhanceTarget& Target);
	virtual void RequestClose() override;

	const FVBCraftingView& GetView() const { return View; }

	// 화면의 버튼이 쓴다(표시용 - 결정은 서버). 소유 클라이언트에서 보이는 수로 같은 판정을 만든다.
	EVBCraftResult PreviewCraft(const FVBCraftingViewEntry& ViewEntry) const;
	// OutCosts = 다음 단계의 비용(재료 + 돈 줄). 판정이 비용까지 가지 못했으면 비어 있다.
	EVBEnhanceResult PreviewEnhance(const FVBEnhanceTarget& Target, TArray<FVBItemAmount>* OutCosts = nullptr) const;

	// 이 플레이어가 가졌나. 무기 = 폰의 무기 상태(가진 무기 목록), 아이템 = 인벤토리에 1 개 이상.
	bool IsTargetOwned(const FVBEnhanceTarget& Target) const;

	// 판정의 사실 모으기. 서버와 표시가 같은 것을 쓴다.
	static FVBCraftQuote BuildCraftQuote(const FVBCraftRecipe& Recipe, bool bAvailable, const UVBInventoryComponent& Inventory,
		const UVBItemDefinition* Currency);
	static FVBEnhanceQuote BuildEnhanceQuote(const FVBEnhanceTarget& Target, bool bOwned, const UVBInventoryComponent& Inventory,
		const UVBEnhancementComponent& Enhancement, const UVBItemDefinition* Currency, TArray<FVBItemAmount>& OutCosts);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual const TCHAR* GetSessionLabel() const override { return TEXT("제작대"); }
	virtual bool CanOpenSession(const UObject* Definition) const override;
	virtual void SendView() override;
	virtual void SendSessionClosed() override;

private:
	UFUNCTION(Server, Reliable)
	void ServerCraft(const UVBCraftingStationDefinition* Station, int32 RecipeIndex);

	UFUNCTION(Server, Reliable)
	void ServerEnhance(const UVBCraftingStationDefinition* Station, const FVBEnhanceTarget& Target);

	UFUNCTION(Server, Reliable)
	void ServerClose();

	UFUNCTION(Client, Reliable)
	void ClientShowCrafting(const FVBCraftingView& InView);

	UFUNCTION(Client, Reliable)
	void ClientCloseCrafting();

	const UVBCraftingStationDefinition* GetServerStation() const;

	// 소유 클라이언트가 그릴 데이터. 리슨 호스트에서 서버 상태와 섞이지 않게 따로 둔다.
	UPROPERTY()
	FVBCraftingView View;
};
