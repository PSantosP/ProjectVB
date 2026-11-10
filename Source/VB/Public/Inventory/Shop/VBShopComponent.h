// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/VBStationSessionComponent.h"
#include "Inventory/Shop/VBShopRules.h"
#include "VBShopComponent.generated.h"

class UVBEconomyConfig;
class UVBItemDefinition;
class UVBShopDefinition;
struct FVBShopEntry;

/** 보기의 한 줄. 서버가 계산한 것 그대로다 - 클라가 값을 계산하지 않는다. */
USTRUCT()
struct VB_API FVBShopViewEntry
{
	GENERATED_BODY()

	// 상점 정의의 Entries 안 번호(아이템, 이름, 설명은 거기서 읽는다).
	UPROPERTY()
	int32 EntryIndex = INDEX_NONE;

	UPROPERTY()
	int32 UnitPrice = 0;

	// INDEX_NONE = 무한.
	UPROPERTY()
	int32 Remaining = INDEX_NONE;

	// 줄의 조건. 거짓인데 보이는 줄은 bShowWhenUnavailable 인 것뿐이다(숨는 줄은 보기에 없다).
	UPROPERTY()
	bool bAvailable = true;
};

/** 소유 클라이언트가 그릴 현재 거래. 열려 있지 않으면 IsActive() 가 거짓이다. */
USTRUCT()
struct VB_API FVBShopView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBShopDefinition> Shop = nullptr;

	UPROPERTY()
	TArray<FVBShopViewEntry> Entries;

	bool IsActive() const { return Shop != nullptr; }
	const FVBShopEntry* GetEntry(const FVBShopViewEntry& ViewEntry) const;
};

/**
 * 거래 진행을 관리한다(AVBPlayerController 에 붙음). 서버가 열린 상점을 보유하고, 클라이언트는 받은 뷰를 그리며 요청만 보낸다.
 * 사기는 ServerBuy 한 곳에서 다 확인한 뒤 바꾼다. 요청에 상점을 함께 보내 닫힌 뒤 도착한 요청을 버린다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBShopComponent : public UVBStationSessionComponent
{
	GENERATED_BODY()

public:
	UVBShopComponent();

	// 서버에서만. 이미 열려 있거나 경제 설정이 없으면 false. Merchant 는 상인(사라지면 거래가 닫힌다) - 없어도 된다.
	bool OpenShop(const UVBShopDefinition* Shop, AActor* Merchant);

	// 서버에서만. 열려 있지 않으면 아무것도 안 한다.
	void CloseShop();

	bool IsShopOpen() const { return IsSessionOpen(); }

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestBuy(const UVBItemDefinition* Item, int32 Count);
	virtual void RequestClose() override;

	// 소유 클라이언트에서 그릴 데이터.
	const FVBShopView& GetView() const { return View; }

	// 화면의 사기 버튼이 쓴다(표시용 - 결정은 ServerBuy). 소유 클라이언트에서 보이는 수로 판정을 만든다.
	EVBPurchaseResult PreviewPurchase(const FVBShopViewEntry& ViewEntry, int32 Count) const;

	// 돈으로 쓰는 아이템(경제 설정). 화면이 가진 돈을 보이는 데 쓴다. 설정이 없으면 nullptr.
	static const UVBItemDefinition* GetCurrency();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual const TCHAR* GetSessionLabel() const override { return TEXT("상점"); }
	virtual bool CanOpenSession(const UObject* Definition) const override;
	virtual void SendView() override;
	virtual void SendSessionClosed() override;

private:
	UFUNCTION(Server, Reliable)
	void ServerBuy(const UVBShopDefinition* Shop, const UVBItemDefinition* Item, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerClose();

	UFUNCTION(Client, Reliable)
	void ClientShowShop(const FVBShopView& InView);

	UFUNCTION(Client, Reliable)
	void ClientCloseShop();

	// 서버: 지금 상점에서 이 플레이어에게 걸리는 배율(경제 전역 규칙 x 상점 규칙). 줄마다 같으므로 한 번 센다.
	float ComputeMultiplier(const UVBEconomyConfig& Economy, const FVBRuleContext& Context) const;

	const UVBShopDefinition* GetServerShop() const;

	// 소유 클라이언트가 그릴 데이터. 리슨 호스트에서 서버 상태와 섞이지 않게 따로 둔다.
	UPROPERTY()
	FVBShopView View;
};
