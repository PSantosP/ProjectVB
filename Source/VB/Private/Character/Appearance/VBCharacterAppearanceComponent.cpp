#include "Character/Appearance/VBCharacterAppearanceComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/Appearance/VBAppearancePiece.h"
#include "Character/Appearance/VBCharacterAppearance.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Traversal/VBTraversalComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBCharacterAppearanceComponent::UVBCharacterAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UVBCharacterAppearanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBCharacterAppearanceComponent, AppearanceId);
	DOREPLIFETIME(UVBCharacterAppearanceComponent, EquippedPieces);
}

void UVBCharacterAppearanceComponent::OnRegister()
{
	Super::OnRegister();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnControllerChanged);
		SyncEquipmentSourceFromPawn();
	}
}

void UVBCharacterAppearanceComponent::OnUnregister()
{
	BodyFade.Reset();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnControllerChanged);
	}
	// 등록 해제는 EndPlay 를 부르지 않는다 - 장비, 저장 완료 구독도 여기서 푼다(표시는 그대로 둔다).
	UnbindEquipmentSource();
	Super::OnUnregister();
}

void UVBCharacterAppearanceComponent::HandlePawnControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	SyncEquipmentSourceFromPawn();
}

void UVBCharacterAppearanceComponent::SyncEquipmentSourceFromPawn()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();
	// 편집기 미리보기 월드에서 등록돼도 출처를 건드리지 않는다. 권위는 SetEquipmentSource 가 다시 본다.
	if (!Pawn || !World || !World->IsGameWorld() || !Pawn->HasAuthority())
	{
		return;
	}

	SetEquipmentSource(UVBEquipmentComponent::FindFor(Pawn->GetPlayerState()));
}

void UVBCharacterAppearanceComponent::BeginPlay()
{
	Super::BeginPlay();
	// 늦게 들어온 클라는 복제된 현재 값만으로 복구한다 - OnRep 이 BeginPlay 앞에 왔으면 그때는 메시가 없었을 수 있다.
	if (AppearanceId.IsValid())
	{
		ApplyAppearance();
	}
}

void UVBCharacterAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindEquipmentSource();
	++RequestGeneration; // 진행 중인 로드 응답을 무효로
	++PieceGeneration;
	DefinitionHandle.Reset();
	ClassHandle.Reset();
	PieceHandle.Reset();
	ClearVisual();
	Super::EndPlay(EndPlayReason);
}

void UVBCharacterAppearanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	BodyFade.Tick(DeltaTime);
	SetComponentTickEnabled(IsVisualActive() || BodyFade.IsActive());
	// 히트스톱은 본체 액터의 CustomTimeDilation 만 바꾼다(UVBTimeDilationSubsystem). 겉모습은 별도 액터라 그대로 옮긴다.
	AActor* Child = VisualChild ? VisualChild->GetChildActor() : nullptr;
	if (Child && GetOwner() && Child->CustomTimeDilation != GetOwner()->CustomTimeDilation)
	{
		Child->CustomTimeDilation = GetOwner()->CustomTimeDilation;
	}
}

uint64 UVBCharacterAppearanceComponent::BeginBodyFade(const UVBBodyFadeConfig* Config, float Duration)
{
	if (!IsRegistered() || !GetOwner() || GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		return 0;
	}

	TArray<USkeletalMeshComponent*> Meshes;
	GatherVisibleMeshes(GetOwner(), Meshes);
	const uint64 Request = BodyFade.Begin(Meshes, Config, Duration);
	SetComponentTickEnabled(IsVisualActive() || BodyFade.IsActive());
	return Request;
}

void UVBCharacterAppearanceComponent::EndBodyFade(uint64 Request)
{
	BodyFade.End(Request);
	SetComponentTickEnabled(IsVisualActive() || BodyFade.IsActive());
}

bool UVBCharacterAppearanceComponent::CanChangeAppearance() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	VBBodyControlPolicy::FAppearanceSwapFacts Facts;
	Facts.bDead = UVBAbilitySystemStatics::IsActorDead(Owner);
	Facts.bFinisherScene = UVBFinisherComponent::IsActorInScene(Owner);
	if (const auto* Ragdoll = Owner->FindComponentByClass<UVBPlayerRagdollComponent>())
	{
		Facts.bPlayerRagdoll = Ragdoll->IsRagdollActive();
	}

	if (const auto* Traversal = Owner->FindComponentByClass<UVBTraversalComponent>())
	{
		Facts.bTraversalAction = Traversal->IsDoingTraversal();
		Facts.bTraversalFacing = Traversal->OwnsCapsuleFacing();
	}

	if (const auto* SmartObject = Owner->FindComponentByClass<UVBGASPSmartObjectComponent>())
	{
		Facts.bSmartObjectInteracting = SmartObject->IsInteracting();
	}

	if (const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Owner)))
	{
		Facts.bAttacking = UVBAbilitySystemStatics::IsAttacking(ASC);
		Facts.bDodging = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Dodging);
		Facts.bInteracting = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Interacting);
		Facts.bWeaponTransition = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Weapon_Summoning)
			|| ASC->HasMatchingGameplayTag(VBGameplayTags::State_Weapon_Sheathing);
	}

	return !VBBodyControlPolicy::BlocksAppearanceSwap(Facts);
}

bool UVBCharacterAppearanceComponent::SetAppearance(const FPrimaryAssetId& NewId)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return false;
	}
	// 클라가 보낸 ID 는 목록(에셋 매니저)에 있는 VBAppearance 만 받는다 - 임의 경로를 받지 않는다.
	if (NewId.IsValid() && (NewId.PrimaryAssetType != UVBCharacterAppearance::GetAppearanceAssetType()
		|| !UAssetManager::Get().GetPrimaryAssetPath(NewId).IsValid()))
	{
		VB_LOG(Warning, "Appearance: %s 는 겉모습 목록에 없다 - 거절", *NewId.ToString());
		return false;
	}

	if (!CanChangeAppearance())
	{
		VB_LOG(Log, "Appearance: %s 가 지금 몸을 쓰는 중이라 교체를 거절한다(평상시에만)", *GetNameSafe(GetOwner()));
		return false;
	}

	if (NewId == AppearanceId)
	{
		return true;
	}

	AppearanceId = NewId;
	ApplyAppearance(); // 권위 쪽에서는 OnRep 이 안 불린다
	return true;
}

void UVBCharacterAppearanceComponent::ServerRequestAppearance_Implementation(FPrimaryAssetId NewId)
{
	SetAppearance(NewId);
}

void FVBEffect_SetAppearance::Apply(const FVBRuleContext& Context) const
{
	APawn* Pawn = Context.Instigator ? Context.Instigator->GetPawn() : nullptr;
	UVBCharacterAppearanceComponent* Component = Pawn ? Pawn->FindComponentByClass<UVBCharacterAppearanceComponent>() : nullptr;
	if (!Component)
	{
		VB_LOG(Warning, "효과(겉모습 바꾸기): 부른 플레이어의 폰 또는 겉모습 컴포넌트가 없다 - %s 로 바꾸지 않음", *Appearance.ToString());
		return;
	}

	if (!Component->SetAppearance(Appearance))
	{
		VB_LOG(Warning, "효과(겉모습 바꾸기): %s 가 %s 로의 교체를 거절했다", *GetNameSafe(Pawn), *Appearance.ToString());
	}
}

void UVBCharacterAppearanceComponent::OnRep_Appearance()
{
	ApplyAppearance();
}

void UVBCharacterAppearanceComponent::SetEquipmentSource(UVBEquipmentComponent* Source)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (Source && EquipmentSource.Get() == Source)
	{
		RefreshEquippedPieces();
		return;
	}

	UnbindEquipmentSource();
	EquipmentSource = Source;
	if (Source)
	{
		EquipmentChangedHandle = Source->OnEquipmentChanged.AddUObject(this, &ThisClass::RefreshEquippedPieces);
		if (UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr)
		{
			EquipmentSaveRegistry = Registry;
			EquipmentSaveFinalizedHandle = Registry->OnSaveFinalized.AddUObject(this, &ThisClass::HandleEquipmentSaveFinalized);
		}
	}

	RefreshEquippedPieces();
}

void UVBCharacterAppearanceComponent::UnbindEquipmentSource()
{
	if (UVBEquipmentComponent* Source = EquipmentSource.Get())
	{
		Source->OnEquipmentChanged.Remove(EquipmentChangedHandle);
	}

	if (UVBSaveParticipantSubsystem* Registry = EquipmentSaveRegistry.Get())
	{
		Registry->OnSaveFinalized.Remove(EquipmentSaveFinalizedHandle);
	}

	EquipmentSource.Reset();
	EquipmentSaveRegistry.Reset();
	EquipmentChangedHandle.Reset();
	EquipmentSaveFinalizedHandle.Reset();
	bEquipmentRefreshPending = false;
}

void UVBCharacterAppearanceComponent::HandleEquipmentSaveFinalized()
{
	bEquipmentRefreshPending = false;
	RefreshEquippedPieces();
}

void UVBCharacterAppearanceComponent::RefreshEquippedPieces()
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	const UVBSaveParticipantSubsystem* Registry = EquipmentSaveRegistry.Get();
	if (Registry && (Registry->IsApplyingSave() || bEquipmentRefreshPending))
	{
		bEquipmentRefreshPending = true;
		return;
	}

	TArray<VBAppearancePieceRules::FEquippedPiece> Input;
	TArray<TStrongObjectPtr<UVBAppearancePiece>> Loaded;
	if (const UVBEquipmentComponent* Source = EquipmentSource.Get())
	{
		for (int32 Slot = 0; Slot < Source->GetSlotLayout().Num(); ++Slot)
		{
			const UVBItemDefinition* Item = Source->GetEquipped(Slot);
			const FVBItemFragment_Appearance* Fragment = Item ? Item->FindFragment<FVBItemFragment_Appearance>() : nullptr;
			UVBAppearancePiece* Piece = Fragment ? Fragment->Piece.LoadSynchronous() : nullptr;
			if (Piece)
			{
				Loaded.Emplace(Piece); // Small definitions only; never load fitted meshes on this authority path.
			}

			Input.Add({Item, Piece});
		}
	}

	const auto Projection = VBAppearancePieceRules::ProjectEquippedPieces(Input);
	for (const FText& Error : Projection.Errors)
	{
		VB_LOG(Error, "Appearance projection: %s", *Error.ToString());
	}

	TArray<TSoftObjectPtr<UVBAppearancePiece>> Next;
	for (const UVBAppearancePiece* Piece : Projection.Pieces)
	{
		Next.Add(const_cast<UVBAppearancePiece*>(Piece));
	}

	if (Next == EquippedPieces)
	{
		return;
	}

	EquippedPieces = MoveTemp(Next);
	ApplyEquippedPieces(); // G6b2b authority apply; hosts receive no OnRep.
}

void UVBCharacterAppearanceComponent::OnRep_EquippedPieces() { ApplyEquippedPieces(); }
void UVBCharacterAppearanceComponent::ApplyEquippedPieces() { ComposePieces(); }

void UVBCharacterAppearanceComponent::ApplyAppearance()
{
	const int32 Generation = ++RequestGeneration;
	++PieceGeneration; // 옛 겉모습의 조각 응답도 버린다
	DefinitionHandle.Reset();
	ClassHandle.Reset();
	PieceHandle.Reset();
	PendingDefinition = nullptr;
	PendingPieces.Reset();
	PendingDefaultPieces.Reset();

	if (GetNetMode() == NM_DedicatedServer || !HasBegunPlay())
	{
		return; // 전용 서버는 ID 만 든다. BeginPlay 전 OnRep 은 BeginPlay 가 다시 부른다.
	}

	if (!AppearanceId.IsValid())
	{
		ClearVisual();
		OnAppearanceChanged.Broadcast(this);
		return;
	}

	const FSoftObjectPath DefinitionPath = UAssetManager::Get().GetPrimaryAssetPath(AppearanceId);
	if (!DefinitionPath.IsValid())
	{
		VB_LOG(Warning, "Appearance: %s 의 자산 경로가 없다 - 지금 표시를 유지한다", *AppearanceId.ToString());
		return;
	}

	DefinitionHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(DefinitionPath,
		FStreamableDelegate::CreateUObject(this, &UVBCharacterAppearanceComponent::OnDefinitionLoaded, Generation, DefinitionPath));
}

void UVBCharacterAppearanceComponent::OnDefinitionLoaded(int32 Generation, FSoftObjectPath DefinitionPath)
{
	if (Generation != RequestGeneration)
	{
		return;
	}

	UVBCharacterAppearance* Definition = Cast<UVBCharacterAppearance>(DefinitionPath.ResolveObject());
	if (!Definition || Definition->VisualActorClass.IsNull())
	{
		VB_LOG(Warning, "Appearance: %s 를 읽지 못했거나 VisualActorClass 가 비었다 - 지금 표시를 유지한다", *DefinitionPath.ToString());
		return;
	}

	PendingDefinition = Definition;
	TArray<FSoftObjectPath> ClassPaths = { Definition->VisualActorClass.ToSoftObjectPath() };
	if (!Definition->RetargetAnimClass.IsNull())
	{
		ClassPaths.Add(Definition->RetargetAnimClass.ToSoftObjectPath());
	}

	ClassHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(ClassPaths,
		FStreamableDelegate::CreateUObject(this, &UVBCharacterAppearanceComponent::OnVisualClassLoaded, Generation));
}

void UVBCharacterAppearanceComponent::OnVisualClassLoaded(int32 Generation)
{
	if (Generation != RequestGeneration || !PendingDefinition)
	{
		return;
	}

	UClass* VisualClass = PendingDefinition->VisualActorClass.Get();
	if (!VisualClass || !AttachVisual(VisualClass))
	{
		return;
	}

	OnAppearanceChanged.Broadcast(this);
	ComposePieces();
}

void UVBCharacterAppearanceComponent::ComposePieces()
{
	const int32 Generation = ++PieceGeneration;
	PieceHandle.Reset();
	PendingPieces.Reset();
	PendingDefaultPieces.Reset();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FText Unwearable = VBAppearancePieceRules::FindUnwearableReason(ActiveDefinition != nullptr, AppearanceId.IsValid(),
		ActiveDefinition ? ActiveDefinition->CharacterId : NAME_None, EquippedPieces.Num());
	if (!Unwearable.IsEmpty())
	{
		TArray<FString> PieceNames;
		for (const TSoftObjectPtr<UVBAppearancePiece>& Piece : EquippedPieces)
		{
			PieceNames.Add(Piece.GetAssetName());
		}

		VB_LOG(Warning, "Appearance: %s (%s) - %s. 장착한 조각이 보이지 않는다: %s", *GetNameSafe(GetOwner()),
			AppearanceId.IsValid() ? *AppearanceId.ToString() : TEXT("본체"), *Unwearable.ToString(), *FString::Join(PieceNames, TEXT(", ")));
	}

	if (!ActiveDefinition || ActiveDefinition->CharacterId.IsNone() || !PrimaryVisualMesh.IsValid())
	{
		return;
	}

	if (ActiveDefinition->DefaultPieces.IsEmpty())
	{
		VB_LOG(Error, "Appearance: slot-enabled definition has no defaults; keeping the completed pieces.");
		return;
	}

	TArray<FSoftObjectPath> Paths;
	const auto Append = [&](const TArray<TSoftObjectPtr<UVBAppearancePiece>>& Pieces)
	{
		for (const TSoftObjectPtr<UVBAppearancePiece>& Piece : Pieces)
		{
			if (!Piece.IsNull())
			{
				Paths.AddUnique(Piece.ToSoftObjectPath());
			}
		}
	};
	Append(ActiveDefinition->DefaultPieces);
	Append(EquippedPieces);
	if (Paths.IsEmpty() || !Paths.ContainsByPredicate([](const FSoftObjectPath& Path) { return Path.ResolveObject() == nullptr; }))
	{
		OnPiecesLoaded(Generation);
		return;
	}

	PieceHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateUObject(this, &UVBCharacterAppearanceComponent::OnPiecesLoaded, Generation));
}

void UVBCharacterAppearanceComponent::OnPiecesLoaded(int32 Generation)
{
	if (Generation != PieceGeneration || !ActiveDefinition || !PrimaryVisualMesh.IsValid())
	{
		return;
	}

	TArray<const UVBAppearancePiece*> Defaults, Worn;
	for (const TSoftObjectPtr<UVBAppearancePiece>& Soft : ActiveDefinition->DefaultPieces)
	{
		const UVBAppearancePiece* Piece = Soft.Get();
		if (!Piece)
		{
			VB_LOG(Error, "Appearance: required default DA %s could not be loaded; keeping completed pieces.", *Soft.ToString());
			return;
		}

		Defaults.Add(Piece);
		PendingDefaultPieces.Add(Piece);
	}

	for (const TSoftObjectPtr<UVBAppearancePiece>& Soft : EquippedPieces)
	{
		if (const UVBAppearancePiece* Piece = Soft.Get())
		{
			Worn.Add(Piece);
		}
		else
		{
			VB_LOG(Warning, "Appearance: equipped DA %s could not be loaded; using defaults.", *Soft.ToString());
		}
	}

	const auto Choice = VBAppearancePieceRules::ChoosePieces(ActiveDefinition->CharacterId, Defaults, Worn);
	for (const FText& Warning : Choice.Warnings)
	{
		VB_LOG(Warning, "Appearance choice: %s", *Warning.ToString());
	}

	for (const FText& Error : Choice.Errors)
	{
		VB_LOG(Error, "Appearance choice: %s", *Error.ToString());
	}

	if (!Choice.Errors.IsEmpty())
	{
		return;
	}

	for (const UVBAppearancePiece* Piece : Choice.Pieces)
	{
		PendingPieces.Add(Piece);
	}

	TArray<FSoftObjectPath> MeshPaths;
	const auto AppendMeshes = [&](const TArray<TObjectPtr<const UVBAppearancePiece>>& Pieces)
	{
		for (const UVBAppearancePiece* Piece : Pieces)
		{
			const TSoftObjectPtr<USkeletalMesh> Mesh = Piece->FindFittedMesh(ActiveDefinition->CharacterId);
			if (!Mesh.IsNull())
			{
				MeshPaths.AddUnique(Mesh.ToSoftObjectPath());
			}
		}
	};
	AppendMeshes(PendingPieces);
	AppendMeshes(PendingDefaultPieces); // Ready defaults also cover a failed load or runtime skeleton mismatch.
	if (MeshPaths.IsEmpty() || !MeshPaths.ContainsByPredicate([](const FSoftObjectPath& Path) { return Path.ResolveObject() == nullptr; }))
	{
		OnPieceMeshesLoaded(Generation);
		return;
	}
	// 첫 단계 완료 callback 안에서 돌고 PieceHandle 이 그 handle 을 보유하고 있으므로, 돌아갈 때까지 살려 둔다.
	const TSharedPtr<FStreamableHandle> FirstStageHandle = PieceHandle;
	PieceHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MeshPaths,
		FStreamableDelegate::CreateUObject(this, &UVBCharacterAppearanceComponent::OnPieceMeshesLoaded, Generation));
}

void UVBCharacterAppearanceComponent::OnPieceMeshesLoaded(int32 Generation)
{
	USkeletalMeshComponent* Body = PrimaryVisualMesh.Get();
	if (Generation != PieceGeneration || !ActiveDefinition || !Body)
	{
		return;
	}

	const USkeletalMesh* BodyMesh = Body->GetSkeletalMeshAsset();
	if (!BodyMesh)
	{
		return;
	}

	const FName CharacterId = ActiveDefinition->CharacterId;
	const auto Fits = [&](const UVBAppearancePiece* Piece)
	{
		const USkeletalMesh* Mesh = Piece ? Piece->FindFittedMesh(CharacterId).Get() : nullptr;
		return Mesh && Mesh->GetSkeleton() && Mesh->GetSkeleton() == BodyMesh->GetSkeleton();
	};
	TArray<const UVBAppearancePiece*> Prepared;
	TSet<FGameplayTag> Slots;
	for (const UVBAppearancePiece* Candidate : PendingPieces)
	{
		const UVBAppearancePiece* Piece = Candidate;
		if (!Fits(Piece))
		{
			const TObjectPtr<const UVBAppearancePiece>* Default = PendingDefaultPieces.FindByPredicate(
				[&](const UVBAppearancePiece* Entry) { return Entry->AppearanceSlot == Candidate->AppearanceSlot; });
			Piece = Default ? Default->Get() : nullptr;
			VB_LOG(Warning, "Appearance: piece %s failed to load or mismatched the runtime skeleton; using a fitted default.",
				*GetNameSafe(Candidate));
		}

		if (Fits(Piece))
		{
			Prepared.Add(Piece);
			Slots.Add(Piece->AppearanceSlot);
		}
	}

	if (!Slots.Contains(VBGameplayTags::Appearance_Slot_Top) || !Slots.Contains(VBGameplayTags::Appearance_Slot_Bottom))
	{
		VB_LOG(Error, "Appearance: required Top/Bottom pieces are not ready; keeping completed pieces and section visibility.");
		return;
	}
	// ApplyPieces 가 옛 완성 세트를 지우기 전에 고른 조각은 모두 로드됐고 skeleton 이 맞는다.
	BodyFade.Reset();
	ApplyPieces(*Body, CharacterId, Prepared, PieceComponents);
	PendingPieces.Reset();
	PendingDefaultPieces.Reset();
	OnAppearanceChanged.Broadcast(this);
}

int32 UVBCharacterAppearanceComponent::ApplyPieces(USkeletalMeshComponent& Body, FName CharacterId,
	const TArray<const UVBAppearancePiece*>& Pieces,
	TArray<TObjectPtr<USkeletalMeshComponent>>& InOutPieceComponents)
{
	for (USkeletalMeshComponent* Old : InOutPieceComponents)
	{
		if (IsValid(Old))
		{
			Old->DestroyComponent();
		}
	}

	InOutPieceComponents.Reset();
	const USkeletalMesh* BodyMesh = Body.GetSkeletalMeshAsset();
	AActor* Owner = Body.GetOwner();
	if (!BodyMesh || !Owner)
	{
		return 0;
	}
	// 렌더 데이터가 없으면(렌더러 없는 실행) 숨길 섹션을 모른다 - 조각은 끼우고 숨김만 건너뛴다.
	const FSkeletalMeshRenderData* RenderData = BodyMesh->GetResourceForRendering();
	const int32 NumLods = RenderData ? RenderData->LODRenderData.Num() : 0;
	for (int32 LOD = 0; LOD < NumLods; ++LOD)
	{
		Body.ShowAllMaterialSections(LOD);
	}

	TSet<FName> Hidden;
	for (const UVBAppearancePiece* Piece : Pieces)
	{
		USkeletalMesh* Mesh = Piece ? Piece->FindFittedMesh(CharacterId).Get() : nullptr;
		if (!Mesh)
		{
			VB_LOG(Warning, "Appearance: 조각 %s 에 '%s' 의 메시가 없거나 안 읽혔다 - 끼우지 않는다", *GetNameSafe(Piece), *CharacterId.ToString());
			continue;
		}

		if (Mesh->GetSkeleton() != BodyMesh->GetSkeleton())
		{
			// 저장 때 V2 가 기본 조각은 막는다. 여기는 실행 중 바디 메시가 바뀐 경우까지 잡는 두 번째 검사다(설계 §10 P2-4).
			VB_LOG(Warning, "Appearance: 조각 %s 의 스켈레톤(%s)이 몸(%s)과 다르다 - 끼우지 않는다", *GetNameSafe(Piece),
				*GetNameSafe(Mesh->GetSkeleton()), *GetNameSafe(BodyMesh->GetSkeleton()));
			continue;
		}

		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Owner);
		Component->SetupAttachment(&Body);
		Component->SetSkeletalMeshAsset(Mesh);
		// 조각은 애님을 돌리지 않고 바디 메시의 뼈를 그대로 따른다(follower 는 AnimBP 를 돌리지 않는다).
		Component->SetLeaderPoseComponent(&Body);
		// 겉모습은 보이기만 한다 - 충돌은 본체 캡슐, 래그돌의 몫(AttachVisual 과 같은 규칙).
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();
		Owner->AddInstanceComponent(Component);
		InOutPieceComponents.Add(Component);
		for (const FGameplayTag& Section : Piece->HidesSections)
		{
			Hidden.Add(Section.GetTagLeafName());
		}
	}

	// 숨김은 LOD 마다 이름으로 고른 섹션에 - 엔진이 섹션의 LOD 머티리얼 맵을 거쳐 실제 머티리얼을 숨긴다(ShowMaterialSection).
	const TArray<TArray<int32>> HidePerLod = VBAppearancePieceRules::SectionsToHide(
		VBAppearancePieceRules::SectionSlotNamesPerLod(*BodyMesh), Hidden);
	for (int32 LOD = 0; LOD < HidePerLod.Num() && LOD < NumLods; ++LOD)
	{
		const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[LOD].RenderSections;
		for (const int32 Section : HidePerLod[LOD])
		{
			Body.ShowMaterialSection(Sections[Section].MaterialIndex, Section, false, LOD);
		}
	}

	return InOutPieceComponents.Num();
}

bool UVBCharacterAppearanceComponent::AttachVisual(UClass* VisualClass)
{
	USkeletalMeshComponent* Driver = GetDriverMesh();
	if (!Driver)
	{
		return false;
	}

	if (const AActor* CDO = VisualClass->GetDefaultObject<AActor>(); CDO && CDO->GetIsReplicated())
	{
		VB_LOG(Error, "Appearance: %s 는 복제 클래스라 클라에서 안 만들어진다 - 지금 표시를 유지한다", *VisualClass->GetName());
		return false;
	}

	if (!VisualChild)
	{
		VisualChild = NewObject<UChildActorComponent>(GetOwner(), TEXT("VBAppearanceChild"));
		VisualChild->SetIsReplicated(false);
		VisualChild->SetupAttachment(Driver);
		GetOwner()->AddInstanceComponent(VisualChild);
		VisualChild->RegisterComponent();
	}

	UClass* PreviousClass = VisualChild->GetChildActorClass();
	VisualChild->SetChildActorClass(VisualClass);
	AActor* Child = VisualChild->GetChildActor();

	USkeletalMeshComponent* Body = nullptr;
	if (Child)
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Child->GetComponents(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetFName() == PendingDefinition->PrimaryBodyComponent)
			{
				Body = Mesh;
				break;
			}
		}
	}

	if (!Body)
	{
		VB_LOG(Error, "Appearance: %s 에서 주 몸 메시 '%s' 를 못 찾았다 - 이전 표시로 되돌린다",
			*VisualClass->GetName(), *PendingDefinition->PrimaryBodyComponent.ToString());
		VisualChild->SetChildActorClass(PreviousClass);
		return false;
	}

	// 조립한 MetaHuman 처럼 BP 가 리타겟을 안 들고 있으면 자산이 준 애님과 리타겟터 태그를 넣는다(UVBCharacterAppearance 주석).
	//   태그를 먼저 넣는다 - ABP_GenericRetarget 은 초기화 때 첫 태그로 리타겟터를 고른다.
	if (!PendingDefinition->RetargetAnimClass.IsNull())
	{
		UClass* AnimClass = PendingDefinition->RetargetAnimClass.Get();
		if (!AnimClass)
		{
			VB_LOG(Error, "Appearance: 리타겟 애님 %s 를 못 불렀다 - 이전 표시로 되돌린다", *PendingDefinition->RetargetAnimClass.ToString());
			VisualChild->SetChildActorClass(PreviousClass);
			return false;
		}

		Body->ComponentTags.Remove(PendingDefinition->RetargeterTag);
		Body->ComponentTags.Insert(PendingDefinition->RetargeterTag, 0);
		Body->SetAnimInstanceClass(AnimClass);
	}

	// 겉모습은 보이기만 한다 - 충돌은 본체 캡슐, 래그돌, 스마트 오브젝트의 몫이다(mesh-consumers.md 위험 4).
	TArray<UPrimitiveComponent*> Primitives;
	Child->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	ActiveDefinition = PendingDefinition;
	PendingDefinition = nullptr;
	PrimaryVisualMesh = Body;
	SetDriverHidden(true);
	SetComponentTickEnabled(true);
	VB_LOG(Log, "Appearance: %s -> %s (주 몸 %s)", *GetNameSafe(GetOwner()), *VisualClass->GetName(), *Body->GetName());
	return true;
}

void UVBCharacterAppearanceComponent::ClearVisual()
{
	BodyFade.Reset();
	PendingPieces.Reset();
	PendingDefaultPieces.Reset();
	if (VisualChild)
	{
		VisualChild->SetChildActorClass(nullptr);
	}

	PrimaryVisualMesh.Reset();
	ActiveDefinition = nullptr;
	PieceComponents.Reset(); // 겉모습 액터와 함께 사라졌다
	SetDriverHidden(false);
	SetComponentTickEnabled(false);
}

void UVBCharacterAppearanceComponent::SetDriverHidden(bool bHidden)
{
	if (bHidden)
	{
		USkeletalMeshComponent* Driver = GetDriverMesh();
		if (!Driver || HiddenDriver.Get() == Driver)
		{
			return; // A -> B 교체는 원래 값을 다시 저장하지 않는다
		}

		bDriverWasVisible = Driver->IsVisible();
		// 자식(겉모습 자식 액터 컴포넌트)까지 번지면 겉모습도 꺼진다.
		Driver->SetVisibility(false, /*bPropagateToChildren=*/false);
		FVBMeshRefresh::Acquire(Driver);
		HiddenDriver = Driver;
		return;
	}

	if (USkeletalMeshComponent* Driver = HiddenDriver.Get())
	{
		Driver->SetVisibility(bDriverWasVisible, /*bPropagateToChildren=*/false);
		FVBMeshRefresh::Release(Driver);
	}

	HiddenDriver.Reset();
}

USkeletalMeshComponent* UVBCharacterAppearanceComponent::GetDriverMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

USkeletalMeshComponent* UVBCharacterAppearanceComponent::GetPrimaryVisualMesh() const
{
	if (USkeletalMeshComponent* Body = PrimaryVisualMesh.Get())
	{
		return Body;
	}

	return GetDriverMesh();
}

void UVBCharacterAppearanceComponent::GatherVisibleMeshes(const AActor* Owner, TArray<USkeletalMeshComponent*>& OutMeshes)
{
	OutMeshes.Reset();
	if (!Owner || Owner->IsHidden())
	{
		return;
	}

	const UVBCharacterAppearanceComponent* Appearance = Owner->FindComponentByClass<UVBCharacterAppearanceComponent>();
	const USkeletalMeshComponent* Primary = Appearance ? Appearance->GetPrimaryVisualMesh() : nullptr;
	if (Appearance && !Primary)
	{
		return;
	}

	const AActor* VisualOwner = Primary ? Primary->GetOwner() : Owner;
	if (!VisualOwner)
	{
		return;
	}

	VisualOwner->GetComponents(OutMeshes, true);
	OutMeshes.RemoveAll([](const USkeletalMeshComponent* Mesh)
	{
		if (!IsValid(Mesh))
		{
			return true;
		}

		const AActor* MeshOwner = Mesh->GetOwner();
		return !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset() || !Mesh->IsVisible()
			|| Mesh->bHiddenInGame || !MeshOwner || MeshOwner->IsHidden();
	});
	OutMeshes.Sort([](const USkeletalMeshComponent& A, const USkeletalMeshComponent& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
}

void UVBCharacterAppearanceComponent::ResolveAttachTarget(FName DriverSocket, USceneComponent*& OutParent, FName& OutSocket) const
{
	OutParent = GetDriverMesh();
	OutSocket = DriverSocket;
	USkeletalMeshComponent* Body = PrimaryVisualMesh.Get();
	if (!Body || !ActiveDefinition)
	{
		return;
	}

	const FName VisualSocket = ActiveDefinition->ResolveSocket(DriverSocket);
	if (!Body->DoesSocketExist(VisualSocket))
	{
		VB_LOG(Warning, "Appearance: 겉모습 %s 에 소켓 %s 가 없다 - 본체에 붙인다(SocketAliases 로 대응시킬 것)",
			*GetNameSafe(ActiveDefinition), *VisualSocket.ToString());
		return;
	}

	OutParent = Body;
	OutSocket = VisualSocket;
}

// 개발용 교체 입력. 화면은 나중(설계 4절). 인자 = 겉모습 자산 이름(DA_Appearance_Kellan) 또는 None.
static void HandleAppearanceCommand(const TArray<FString>& Args, UWorld* World)
{
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UVBCharacterAppearanceComponent* Appearance = Pawn ? Pawn->FindComponentByClass<UVBCharacterAppearanceComponent>() : nullptr;
	if (!Appearance)
	{
		VB_LOG(Warning, "VB.Appearance: 로컬 플레이어 폰에 겉모습 컴포넌트가 없다");
		return;
	}

	const bool bNone = Args.Num() == 0 || Args[0].Equals(TEXT("None"), ESearchCase::IgnoreCase);
	const FPrimaryAssetId Id = bNone ? FPrimaryAssetId()
		: FPrimaryAssetId(UVBCharacterAppearance::GetAppearanceAssetType(), FName(*Args[0]));
	if (Pawn->HasAuthority())
	{
		Appearance->SetAppearance(Id);
	}
	else
	{
		Appearance->ServerRequestAppearance(Id);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GVBAppearanceCommand(
	TEXT("VB.Appearance"),
	TEXT("플레이어 겉모습을 바꾼다. 인자 = 겉모습 자산 이름(예: DA_Appearance_Kellan) 또는 None. 평상시에만 받는다."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleAppearanceCommand));
