#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBMovementConfig.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace VBPlayBotPlayerActions
{
namespace
{
// Blueprint 로 만든 projection 필드는 생성 접미사가 붙을 수 있고, native 필드는 이름 그대로다
FProperty* FindProjectionField(const UStruct* Type, FName Name)
{
	if (FProperty* Exact = Type->FindPropertyByName(Name))
	{
		return Exact;
	}

	const FString Prefix = Name.ToString() + TEXT("_");
	FProperty* Match = nullptr;
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		if (!It->GetName().StartsWith(Prefix))
		{
			continue;
		}

		if (Match)
		{
			return nullptr; // Ambiguous projection must fail rather than choose an arbitrary field.
		}

		Match = *It;
	}

	return Match;
}

class FVBTurnInPlaceAction : public FVBPlayerSequenceAction
{
public:
	FVBTurnInPlaceAction() : FVBPlayerSequenceAction(TEXT("TurnInPlace")) {}
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		if (!Configure(Ctx, Error) || !Positive(Ctx, TEXT("LookSeconds"), LookSeconds, Error)
			|| !Positive(Ctx, TEXT("LookMagnitude"), LookMagnitude, Error) || !Positive(Ctx, TEXT("SettleSeconds"), Settle,
				Error))
		{
			return false;
		}

		if (LookMagnitude > 1.0) { Error = TEXT("LookMagnitude must be in (0,1]"); return false; }
		if (!VBPlayBotSkills::EnsureAction(Ctx, TEXT("Input.WeaponSlot.3"), Error) || !ResolveSchema(Ctx, Error)
			|| !LoadDatabases(Ctx, Error))
		{
			return false;
		}

		AddNeutral();
		Sequence.Then<FVBSkillEquip>(TEXT("Fighter"), FName(TEXT("Input.WeaponSlot.3")));
		Sequence.Then<FVBSkillPress>(TEXT("sheathe Fighter for unarmed turn"), FName(TEXT("Input.WeaponSlot.3")));
		Sequence.Then<FVBSkillWait>(TEXT("weapon transition settle"), Settle);
		AddTurn(TEXT("Unarmed"));
		Sequence.Then<FVBSkillEquip>(TEXT("Fighter"), FName(TEXT("Input.WeaponSlot.3")));
		Sequence.Then<FVBSkillWait>(TEXT("Fighter settle"), Settle);
		AddTurn(TEXT("Fighter"));
		AddNeutral();
		return true;
	}

private:
	bool ResolveSchema(FVBPlayBotContext& Ctx, FString& Error)
	{
		const USkeletalMeshComponent* Mesh = Ctx.Player->GetMesh();
		Main = Mesh ? Cast<UVBAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
		Linked = Main.IsValid() ? Main->GetGASPLocomotionInstance() : nullptr;
		Config = Body->GetMovementConfig();
		FString ThresholdName;
		if (!Ctx.Config->RequireString(TEXT("TurnThresholdProperty"), ThresholdName, Error))
		{
			return false;
		}

		if (!Main.IsValid() || !Linked.IsValid() || !Config.IsValid()) { Error = TEXT("turn requires live main/linked/config owners at Setup"); return false; }
		UClass* LinkedClass = Linked->GetClass();
		StateField = CastField<FStructProperty>(FindProjectionField(LinkedClass, TEXT("CMCState")));
		BlendField = CastField<FStructProperty>(FindProjectionField(LinkedClass, TEXT("CMCBlendInputs")));
		RootField = CastField<FStructProperty>(FindProjectionField(LinkedClass, TEXT("RootTransform")));
		OriginalAnimField = CastField<FObjectPropertyBase>(FindProjectionField(LinkedClass, TEXT("CurrentSelectedAnim")));
		OriginalDatabaseField = CastField<FObjectPropertyBase>(FindProjectionField(LinkedClass, TEXT("CurrentSelectedDatabase")));
		SourceField = FindFProperty<FObjectPropertyBase>(FVBGASPCMCChooserOutput::StaticStruct(), TEXT("SourceDatabase"));
		ThresholdField = FindFProperty<FNumericProperty>(Config->GetClass(), FName(*ThresholdName));
		if (!StateField || StateField->Struct != FVBGASPCMCState::StaticStruct()
			|| !BlendField || BlendField->Struct != FVBGASPCMCBlendInputs::StaticStruct()
			|| !RootField || RootField->Struct != TBaseStructure<FTransform>::Get()
			|| !OriginalAnimField || !OriginalDatabaseField || !SourceField || !ThresholdField || !ThresholdField->IsFloatingPoint())
		{
			Error = FString::Printf(TEXT("turn schema: CMCState=%s CMCBlendInputs=%s RootTransform=%s CurrentSelectedAnim=%s CurrentSelectedDatabase=%s SourceDatabase=%s Threshold=%s"),
				*GetPathNameSafe(StateField ? StateField->Struct : nullptr), *GetPathNameSafe(BlendField ? BlendField->Struct : nullptr), *GetPathNameSafe(RootField ? RootField->Struct : nullptr),
				OriginalAnimField ? TEXT("present") : TEXT("missing"), OriginalDatabaseField ? TEXT("present") : TEXT("missing"),
				SourceField ? TEXT("present") : TEXT("missing"), ThresholdField && ThresholdField->IsFloatingPoint() ? TEXT("floating") : TEXT("missing-or-wrong-type"));
			return false;
		}

		const double Threshold = ThresholdField->GetFloatingPointPropertyValue(ThresholdField->ContainerPtrToValuePtr<void>(Config.Get()));
		if (!FMath::IsFinite(Threshold) || Threshold <= 0.0) { Error = TEXT("live turn threshold is invalid"); return false; }
		return true;
	}

	bool LoadDatabases(FVBPlayBotContext& Ctx, FString& Error)
	{
		UClass* DatabaseClass = FindObject<UClass>(nullptr, TEXT("/Script/PoseSearch.PoseSearchDatabase"));
		const FArrayProperty* EntriesField = DatabaseClass ? FindFProperty<FArrayProperty>(DatabaseClass,
			TEXT("DatabaseAnimationAssets")) : nullptr;
		const FStructProperty* Entry = EntriesField ? CastField<FStructProperty>(EntriesField->Inner) : nullptr;
		const FObjectPropertyBase* AssetField = Entry ? FindFProperty<FObjectPropertyBase>(Entry->Struct, TEXT("AnimAsset")) : nullptr;
		const FBoolProperty* EnabledField = Entry ? FindFProperty<FBoolProperty>(Entry->Struct, TEXT("bEnabled")) : nullptr;
		if (!DatabaseClass || !EntriesField || !Entry || !AssetField || !EnabledField)
		{ Error = TEXT("PoseSearch database animation membership schema is unavailable"); return false; }
		for (const FString& Name : {FString(TEXT("Unarmed")), FString(TEXT("Fighter"))})
		{
			const TArray<FString> Paths = Ctx.Config->GetArray(*(Name + TEXT("TurnDatabases")));
			if (Paths.IsEmpty()) { Error = Name + TEXT("TurnDatabases requires actual allowed database identities"); return false; }
			for (const FString& Path : Paths)
			{
				UObject* Database = LoadObject<UObject>(nullptr, *Path);
				if (!Database || !Database->IsA(DatabaseClass)) { Error = TEXT("turn database did not load with the declared class: ") + Path; return false; }
				Databases.FindOrAdd(Name).Add(Database); LoadedDatabases.Emplace(Database);
				TSet<const UObject*>& Members = DatabaseAnimations.FindOrAdd(Database);
				FScriptArrayHelper Entries(EntriesField, EntriesField->ContainerPtrToValuePtr<void>(Database));
				for (int32 Index = 0; Index < Entries.Num(); ++Index)
				{
					const void* Value = Entries.GetRawPtr(Index);
					const UObject* Animation = AssetField->GetObjectPropertyValue_InContainer(Value);
					if (Animation && EnabledField->GetPropertyValue_InContainer(Value))
					{
						Members.Add(Animation);
					}
				}

				if (Members.IsEmpty()) { Error = TEXT("turn database has no enabled animation identities: ") + Path; return false; }
			}
		}

		return true;
	}

	void AddTurn(FString Name)
	{
		Sequence.Then<FVBSkillDo>(Name + TEXT(" turn begin"), [this, Name](FVBPlayBotContext& C, FString& Error)
		{
			const UVBWeaponStateComponent* Weapon = UVBWeaponStateComponent::FindFor(Body.Get());
			const bool bArmed = Weapon && Weapon->IsWeaponSummoned();
			if (!Weapon || bArmed != (Name == TEXT("Fighter"))) { Error = TEXT("actual weapon posture does not match turn fixture"); return false; }
			if (UVBLocomotionIntentComponent::IsAimingFor(Body.Get()) || Has(C, VBGameplayTags::State_Combat_Guarding)) { Error = TEXT("turn action requires no aim and no guard"); return false; }
			Variant = Name; Missing.Empty(); RequestFrame = -1; QualifiedFrames = Requests = 0; bSelectionOnRequest = false; FirstEvidence.Empty(); return true;
		});
		Sequence.Then<FVBSkillLook>(Name + TEXT(" no-aim camera turn"), FVector2D(LookMagnitude, 0.0), LookSeconds);
		Sequence.Then<FVBSkillWait>(Name + TEXT(" turn settle"), Settle);
		Sequence.Then<FVBSkillDo>(Name + TEXT(" turn recovery receipt"), [this, Name](FVBPlayBotContext& C, FString&)
		{
			const FString Evidence = FirstEvidence.IsEmpty()
				? FString::Printf(TEXT("{\"requestFrame\":-1,\"qualified\":%d,\"requests\":%d,\"missing\":\"%s\"}"), QualifiedFrames, Requests, *Missing.ReplaceCharWithEscapedChar()) : FirstEvidence;
			if (RequestFrame < 0)
			{
				C.Checks->Expect(TEXT("RG.TurnInPlace.NoAimRequest.") + Name, false, true, C.Frame, Evidence);
				C.Checks->Expect(TEXT("RG.TurnInPlace.SelectionOnRequest.") + Name, false, true, C.Frame, Evidence);
			}

			C.Checks->Expect(TEXT("RG.TurnInPlace.SelectedAndRecovers.") + Name, RequestFrame >= 0 && bSelectionOnRequest
				&& Missing.IsEmpty() && Neutral(C), true, C.Frame, Evidence);
			Variant.Empty(); return true;
		});
	}

	void Observe(FVBPlayBotContext& Ctx) override
	{
		if (Variant.IsEmpty() || !Missing.IsEmpty())
		{
			return;
		}

		const USkeletalMeshComponent* Mesh = Ctx.Player->GetMesh();
		if (!Main.IsValid() || !Linked.IsValid() || !Config.IsValid() || !Mesh || Mesh->GetAnimInstance() != Main.Get()
			|| Main->GetGASPLocomotionInstance() != Linked.Get() || Body->GetMovementConfig() != Config.Get())
		{ Missing = TEXT("turn owner identity changed after Setup"); return; }
		const FVBGASPCMCState* State = StateField->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked.Get());
		const FTransform* Root = RootField->ContainerPtrToValuePtr<FTransform>(Linked.Get());
		const double Threshold = ThresholdField->GetFloatingPointPropertyValue(ThresholdField->ContainerPtrToValuePtr<void>(Config.Get()));
		if (!FMath::IsFinite(Threshold) || Threshold <= 0.0) { Missing = TEXT("live turn threshold became invalid"); return; }
		const double Yaw = FMath::Abs(FMath::FindDeltaAngleDegrees(Root->Rotator().Yaw, Ctx.Player->GetActorRotation().Yaw));
		const UCharacterMovementComponent* Move = Ctx.Player->GetCharacterMovement();
		const bool bNoAim = !UVBLocomotionIntentComponent::IsAimingFor(Body.Get()) && !Main->bUseGuardLocomotionPose;
		const bool bQualified = bNoAim && FVBPlayBotContext::IsGrounded(Ctx.Player) && Move
			&& Move->bUseControllerDesiredRotation && !Move->bOrientRotationToMovement && Yaw >= Threshold;
		if (!bQualified)
		{
			return;
		}

		++QualifiedFrames;
		if (!UVBGASPLocomotionLibrary::ShouldTurnInPlace(Linked.Get()))
		{
			return;
		}

		++Requests;
		if (RequestFrame >= 0)
		{
			return;
		}

		RequestFrame = Ctx.Frame;
		const FVBGASPCMCBlendInputs* Playback = BlendField->ContainerPtrToValuePtr<FVBGASPCMCBlendInputs>(Linked.Get());
		const UObject* Animation = State->bUsesWeaponLocomotion ? Playback->Anim.Get() : OriginalAnimField->GetObjectPropertyValue_InContainer(Linked.Get());
		const UObject* Database = State->bUsesWeaponLocomotion ? SourceField->GetObjectPropertyValue_InContainer(&State->LastSelection) : OriginalDatabaseField->GetObjectPropertyValue_InContainer(Linked.Get());
		const TSet<const UObject*>* Members = DatabaseAnimations.Find(Database);
		bSelectionOnRequest = Animation && Databases.FindChecked(Variant).Contains(Database) && Members && Members->Contains(Animation);
		FirstEvidence = FString::Printf(TEXT("{\"requestFrame\":%lld,\"qualified\":true,\"requested\":true,\"yaw\":%.4f,\"threshold\":%.4f,\"databaseContainsAnimation\":%s,\"animation\":\"%s\",\"database\":\"%s\"}"),
			RequestFrame, Yaw, Threshold, bSelectionOnRequest ? TEXT("true") : TEXT("false"), *GetPathNameSafe(Animation).ReplaceCharWithEscapedChar(), *GetPathNameSafe(Database).ReplaceCharWithEscapedChar());
		// 이 값은 한 번만 잡는다 - 나중의 무관한 선택이 첫 요청 프레임을 고쳐 주지 못한다
		Ctx.Checks->Expect(TEXT("RG.TurnInPlace.NoAimRequest.") + Variant, true, true, Ctx.Frame, FirstEvidence);
		Ctx.Checks->Expect(TEXT("RG.TurnInPlace.SelectionOnRequest.") + Variant, bSelectionOnRequest, true, Ctx.Frame, FirstEvidence);
	}

	TWeakObjectPtr<UVBAnimInstance> Main;
	TWeakObjectPtr<UAnimInstance> Linked;
	TWeakObjectPtr<const UVBMovementConfig> Config;
	const FStructProperty* StateField = nullptr;
	const FStructProperty* BlendField = nullptr;
	const FStructProperty* RootField = nullptr;
	const FObjectPropertyBase* OriginalAnimField = nullptr;
	const FObjectPropertyBase* OriginalDatabaseField = nullptr;
	const FObjectPropertyBase* SourceField = nullptr;
	const FNumericProperty* ThresholdField = nullptr;
	TMap<FString, TSet<const UObject*>> Databases;
	TMap<const UObject*, TSet<const UObject*>> DatabaseAnimations;
	TArray<TStrongObjectPtr<UObject>> LoadedDatabases;
	FString Variant, Missing, FirstEvidence;
	double LookSeconds = 0.0, LookMagnitude = 0.0, Settle = 0.0;
	int32 QualifiedFrames = 0, Requests = 0;
	int64 RequestFrame = -1;
	bool bSelectionOnRequest = false;
};
}
TUniquePtr<FVBPlayBotPlayerAction> CreateTurnInPlace()
{
	return MakeUnique<FVBTurnInPlaceAction>();
}
}
