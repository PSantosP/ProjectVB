// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBToggleProp.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBWorldItem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Unit/VBStoryTestWorld.h"

// 상호작용 완료의 결과 - 소유 액터의 동작이 실패를 알리거나 완료 효과가 못 해내면 NotifyInteracted 가 false 이고 완료 알림이 없다.
// 대조: ActionFailure 를 안 보면 WorldItemFullInventory, 일회용 소품을 다시 켜질 때 잠그면 ToggleOneShotNoStory 가 실패한다.
// 사실 태그는 연산과 무관해 네이티브 태그를 빌린다.

namespace VBInteractResultTest
{
	// 배치에서 적는 protected 칸이라 리플렉션으로 채운다.
	bool AddCompletionSetFact(FAutomationTestBase& Test, UVBInteractableComponent& Target, const FGameplayTag& Fact)
	{
		const FArrayProperty* Property = FindFProperty<FArrayProperty>(UVBInteractableComponent::StaticClass(), TEXT("CompletionEffects"));
		if (!Test.TestNotNull(TEXT("CompletionEffects property"), Property))
		{
			return false;
		}

		FVBEffect_SetFact SetFact;
		SetFact.Fact = Fact;
		SetFact.bOn = true;
		Property->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(&Target)->Add(FInstancedStruct::Make(SetFact));
		return true;
	}

	// 설정이 비면 시작 때 경고가 난다 - 빈 설정(탭, 동작 없음) 하나를 꽂는다.
	bool UseBlankConfig(FAutomationTestBase& Test, UVBInteractableComponent& Target)
	{
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(UVBInteractableComponent::StaticClass(), TEXT("Config"));
		if (!Test.TestNotNull(TEXT("Config property"), Property))
		{
			return false;
		}

		Property->SetObjectPropertyValue_InContainer(&Target, NewObject<UVBInteractableConfig>(GetTransientPackage()));
		return true;
	}

	ACharacter* SpawnCharacter(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<ACharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	// 빙의된 캐릭터 + 그 PlayerState 의 인벤토리. 줍기가 폰 -> PlayerState 로 인벤토리를 찾는 실제 길이다.
	ACharacter* SpawnPlayerWithInventory(FAutomationTestBase& Test, UWorld& World, UVBInventoryComponent*& OutInventory)
	{
		APlayerController* Controller = World.SpawnActor<APlayerController>();
		APlayerState* State = World.SpawnActor<APlayerState>();
		ACharacter* Pawn = SpawnCharacter(World);
		if (!Test.TestTrue(TEXT("player actors"), Controller && State && Pawn))
		{
			return nullptr;
		}

		State->SetOwner(Controller);
		Controller->PlayerState = State;
		Controller->Possess(Pawn);
		OutInventory = NewObject<UVBInventoryComponent>(State);
		State->AddInstanceComponent(OutInventory);
		OutInventory->RegisterComponent();
		if (!Test.TestTrue(TEXT("the inventory is found through the pawn"), UVBInventoryComponent::FindFor(Pawn) == OutInventory))
		{
			return nullptr;
		}

		return Pawn;
	}

	UVBItemDefinition* MakeItem(int32 MaxCount)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>(GetTransientPackage());
		Item->ItemId = VBGameplayTags::Melee_Shape_Weapon;
		Item->MaxCount = MaxCount;
		return Item;
	}

	// 땅의 아이템: 무엇, 몇 개, 완료 효과(사실 켜기)를 시작 전에 넣고 시작한다(시작이 줍기 신호를 잇는다).
	AVBWorldItem* SpawnWorldItem(FAutomationTestBase& Test, UWorld& World, UVBItemDefinition* Item, int32 Count, const FGameplayTag& Fact,
		UVBInteractableComponent*& OutTarget)
	{
		AVBWorldItem* Pickup = World.SpawnActor<AVBWorldItem>();
		OutTarget = Pickup ? Pickup->FindComponentByClass<UVBInteractableComponent>() : nullptr;
		const FObjectProperty* ItemProperty = FindFProperty<FObjectProperty>(AVBWorldItem::StaticClass(), TEXT("Item"));
		const FIntProperty* CountProperty = FindFProperty<FIntProperty>(AVBWorldItem::StaticClass(), TEXT("Count"));
		if (!Test.TestNotNull(TEXT("world item"), Pickup) || !Test.TestNotNull(TEXT("world item interactable"), OutTarget)
			|| !Test.TestNotNull(TEXT("Item property"), ItemProperty) || !Test.TestNotNull(TEXT("Count property"),
				CountProperty))
		{
			return nullptr;
		}

		ItemProperty->SetObjectPropertyValue_InContainer(Pickup, Item);
		CountProperty->SetPropertyValue_InContainer(Pickup, Count);
		if (!UseBlankConfig(Test, *OutTarget) || !AddCompletionSetFact(Test, *OutTarget, Fact))
		{
			return nullptr;
		}

		Pickup->DispatchBeginPlay();
		return Pickup;
	}

	// 일회용 소품: 일회용 표시, 완료 효과(사실 켜기)를 시작 전에 넣고 시작한다(시작이 상호작용 신호를 잇는다).
	AVBToggleProp* SpawnOneShotProp(FAutomationTestBase& Test, UWorld& World, const FGameplayTag& Fact,
		UVBInteractableComponent*& OutTarget)
	{
		AVBToggleProp* Prop = World.SpawnActor<AVBToggleProp>();
		OutTarget = Prop ? Prop->FindComponentByClass<UVBInteractableComponent>() : nullptr;
		const FBoolProperty* OneShot = FindFProperty<FBoolProperty>(AVBToggleProp::StaticClass(), TEXT("bOneShot"));
		if (!Test.TestNotNull(TEXT("toggle prop"), Prop) || !Test.TestNotNull(TEXT("toggle prop interactable"), OutTarget)
			|| !Test.TestNotNull(TEXT("bOneShot property"), OneShot))
		{
			return nullptr;
		}

		OneShot->SetPropertyValue_InContainer(Prop, true);
		if (!UseBlankConfig(Test, *OutTarget) || !AddCompletionSetFact(Test, *OutTarget, Fact))
		{
			return nullptr;
		}

		Prop->DispatchBeginPlay();
		return Prop;
	}
}

// 가방이 가득 차 줍기가 다 들어가지 못하면(하나도, 일부만) 완료가 아니다 - 줍기에 걸린 사실은 마지막 하나를 주울 때 켜진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractResultWorldItemFullTest, "VB.Unit.Interaction.Result.WorldItemFullInventory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractResultWorldItemFullTest::RunTest(const FString& Parameters)
{
	using namespace VBInteractResultTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld& World = *Isolated.World;
	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(World);
	UVBInventoryComponent* Inventory = nullptr;
	ACharacter* Player = SpawnPlayerWithInventory(*this, World, Inventory);
	if (!TestNotNull(TEXT("story"), Story) || !Player)
	{
		return false;
	}

	const FGameplayTag Picked = VBGameplayTags::State_Dead;
	UVBItemDefinition* Item = MakeItem( /*MaxCount=*/3);
	TestEqual(TEXT("the bag starts full"), Inventory->AddItem(Item, 3), 3);
	UVBInteractableComponent* Target = nullptr;
	AVBWorldItem* Pickup = SpawnWorldItem(*this, World, Item, /*Count=*/2, Picked, Target);
	if (!Pickup)
	{
		return false;
	}

	if (!TestTrue(TEXT("the story is found through the pickup"), UVBStoryStateComponent::Get(Target) == Story))
	{
		return false;
	}

	// 실패 이유가 다르면(2 개 남음 -> 1 개 남음) 각각 한 번씩 알린다.
	AddExpectedMessagePlain(TEXT("동작 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);

	TestFalse(TEXT("full bag: the interaction does not complete"), Target->NotifyInteracted(Player));
	TestFalse(TEXT("full bag: the completion fact is not set"), Story->HasFact(Picked));
	TestEqual(TEXT("full bag: nothing went in"), Inventory->GetCount(Item), 3);
	TestFalse(TEXT("full bag: the item is not picked"), Target->IsActivated());
	TestTrue(TEXT("full bag: the item can still be picked"), Target->IsInteractionEnabled());
	TestFalse(TEXT("full bag: the item is still visible"), Pickup->IsHidden());

	// 한 칸 비우면 하나만 들어간다 - 아직 하나가 남아 완료가 아니다.
	TestTrue(TEXT("free one"), Inventory->RemoveItem(Item, 1));
	TestFalse(TEXT("partial pickup: the interaction does not complete"), Target->NotifyInteracted(Player));
	TestEqual(TEXT("partial pickup: one went in"), Inventory->GetCount(Item), 3);
	TestFalse(TEXT("partial pickup: the completion fact is not set"), Story->HasFact(Picked));
	TestFalse(TEXT("partial pickup: the item is not picked"), Target->IsActivated());
	TestTrue(TEXT("partial pickup: the rest can still be picked"), Target->IsInteractionEnabled());

	// 남은 하나가 들어갈 자리가 생기면 줍기가 끝나고 그때 사실이 켜진다.
	TestTrue(TEXT("free one more"), Inventory->RemoveItem(Item, 1));
	TestTrue(TEXT("room: the interaction completes"), Target->NotifyInteracted(Player));
	TestEqual(TEXT("room: the last one went in"), Inventory->GetCount(Item), 3);
	TestTrue(TEXT("room: the completion fact is set"), Story->HasFact(Picked));
	TestTrue(TEXT("room: the item is picked"), Target->IsActivated());
	TestFalse(TEXT("room: the picked item is no longer interactable"), Target->IsInteractionEnabled());
	TestTrue(TEXT("room: the picked item is hidden"), Pickup->IsHidden());
	return true;
}

// 줍는 쪽에 인벤토리가 없으면 완료가 아니다 - 사실 저장소가 있어도 사실이 켜지지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractResultWorldItemNoInventoryTest, "VB.Unit.Interaction.Result.WorldItemNoInventory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractResultWorldItemNoInventoryTest::RunTest(const FString& Parameters)
{
	using namespace VBInteractResultTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld& World = *Isolated.World;
	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(World);
	ACharacter* Stranger = SpawnCharacter(World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("character without an inventory"), Stranger))
	{
		return false;
	}

	if (!TestNull(TEXT("no inventory is found"), UVBInventoryComponent::FindFor(Stranger)))
	{
		return false;
	}

	const FGameplayTag Picked = VBGameplayTags::State_Dead;
	UVBInteractableComponent* Target = nullptr;
	AVBWorldItem* Pickup = SpawnWorldItem(*this, World, MakeItem( /*MaxCount=*/3), /*Count=*/1, Picked, Target);
	if (!Pickup)
	{
		return false;
	}

	AddExpectedMessagePlain(TEXT("줍기 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("동작 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("no inventory: the interaction does not complete"), Target->NotifyInteracted(Stranger));
	TestFalse(TEXT("no inventory: the completion fact is not set"), Story->HasFact(Picked));
	TestFalse(TEXT("no inventory: the item is not picked"), Target->IsActivated());
	TestTrue(TEXT("no inventory: the item can still be picked"), Target->IsInteractionEnabled());
	return true;
}

// 일회용 소품의 완료 효과가 해내지 못하면(사실 저장소 없음) 잠기지 않고 다시 쓸 수 있게 남는다. 저장소가 있으면 사실과 함께 잠긴다.
//   상호작용한 쪽은 실제 액터여야 한다 - 없으면 연결 소품으로 쳐서 켜짐이 바뀔 때 바로 잠근다(그 길은 이 시험이 아니다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractResultToggleOneShotTest, "VB.Unit.Interaction.Result.ToggleOneShotNoStory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractResultToggleOneShotTest::RunTest(const FString& Parameters)
{
	using namespace VBInteractResultTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld& World = *Isolated.World;
	ACharacter* User = SpawnCharacter(World);
	if (!TestNotNull(TEXT("interacting character"), User))
	{
		return false;
	}

	const FGameplayTag Opened = VBGameplayTags::State_Encounter_Held;
	UVBInteractableComponent* Target = nullptr;
	if (!SpawnOneShotProp(*this, World, Opened, Target))
	{
		return false;
	}

	if (!TestNull(TEXT("no story in this world yet"), UVBStoryStateComponent::Get(Target)))
	{
		return false;
	}

	// 사실 켜기의 경고 하나, 해내지 못함 둘(효과 적용 길, 상호작용).
	AddExpectedMessagePlain(TEXT("사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("해내지 못했다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("no story: the interaction does not complete"), Target->NotifyInteracted(User));
	TestFalse(TEXT("no story: the one-shot did not turn on"), Target->IsActivated());
	TestTrue(TEXT("no story: the one-shot is not locked (it can be retried)"), Target->IsInteractionEnabled());

	// 같은 소품을 다시 누른다 - 실패한 일회용은 닫힌 채라 처음부터 다시 한다.
	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(World);
	if (!TestNotNull(TEXT("story"), Story))
	{
		return false;
	}

	if (!TestTrue(TEXT("the story is found through the prop"), UVBStoryStateComponent::Get(Target) == Story))
	{
		return false;
	}

	TestTrue(TEXT("retry with a story: the interaction completes"), Target->NotifyInteracted(User));
	TestTrue(TEXT("retry: the completion fact is set"), Story->HasFact(Opened));
	TestTrue(TEXT("retry: the prop is activated"), Target->IsActivated());
	TestFalse(TEXT("retry: the one-shot is locked after its completion"), Target->IsInteractionEnabled());
	return true;
}

// 아무것도 알리지 않는 소유 액터(보통 액터)의 대상은 예전 그대로 끝나고 완료 효과를 낸다. 효과가 없으면 저장소가 없어도 끝난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractResultListenerSilentTest, "VB.Unit.Interaction.Result.ListenerSilent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractResultListenerSilentTest::RunTest(const FString& Parameters)
{
	using namespace VBInteractResultTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld& World = *Isolated.World;
	AActor* Owner = World.SpawnActor<AActor>();
	ACharacter* User = SpawnCharacter(World);
	if (!TestNotNull(TEXT("plain owner"), Owner) || !TestNotNull(TEXT("interacting character"), User))
	{
		return false;
	}

	UVBInteractableComponent* Target = NewObject<UVBInteractableComponent>(Owner);
	Owner->SetRootComponent(Target);
	Owner->AddInstanceComponent(Target);
	Target->RegisterComponent();

	TestTrue(TEXT("no effects, no story: completes"), Target->NotifyInteracted(User));

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(World);
	if (!TestNotNull(TEXT("story"), Story))
	{
		return false;
	}

	const FGameplayTag Done = VBGameplayTags::State_Dead;
	if (!AddCompletionSetFact(*this, *Target, Done))
	{
		return false;
	}

	TestTrue(TEXT("silent owner: completes"), Target->NotifyInteracted(User));
	TestTrue(TEXT("silent owner: the completion fact is set"), Story->HasFact(Done));

	// 완료마다 돈다 - 앞선 완료가 다음 완료를 막지 않는다.
	Story->SetFact(Done, false);
	TestTrue(TEXT("silent owner: completes again"), Target->NotifyInteracted(User));
	TestTrue(TEXT("silent owner: the completion fact is set again"), Story->HasFact(Done));
	return true;
}
#endif
