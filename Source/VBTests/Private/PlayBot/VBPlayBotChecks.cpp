// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotChecks.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace
{
	FString Escape(const FString& In)
	{
		return In.ReplaceCharWithEscapedChar();
	}
}

void FVBPlayBotChecks::SetForceWrong(const FString& CommaList)
{
	TArray<FString> Names;
	CommaList.ParseIntoArray(Names, TEXT(","), /*CullEmpty=*/true);
	for (const FString& Name : Names)
	{
		ForceWrong.Add(Name.TrimStartAndEnd());
	}
}

void FVBPlayBotChecks::Expect(const FString& Name, bool bObserved, bool bExpected, int64 Frame, const FString& EvidenceJson)
{
	Evaluated.Add(Name);
	const bool bForced = ForceWrong.Contains(Name);
	const bool bWanted = bForced ? !bExpected : bExpected;
	const bool bPass = bObserved == bWanted;
	bPass ? ++Passed : ++Failed;
	if (Emit)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"check\",\"name\":\"%s\",\"pass\":%s,\"f\":%lld,\"observed\":%s,\"expected\":%s,\"forced\":%s,\"evidence\":%s}"),
			*Escape(Name), bPass ? TEXT("true") : TEXT("false"), Stamp(Frame), bObserved ? TEXT("true") : TEXT("false"),
			bWanted ? TEXT("true") : TEXT("false"), bForced ? TEXT("true") : TEXT("false"),
			EvidenceJson.IsEmpty() ? TEXT("{}") : *EvidenceJson));
	}
}

void FVBPlayBotChecks::Unavailable(const FString& Name, int64 Frame, const FString& Why)
{
	++Unknown;
	if (Emit)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"check\",\"name\":\"%s\",\"pass\":null,\"f\":%lld,\"why\":\"%s\"}"), *Escape(Name), Stamp(Frame), *Escape(Why)));
	}
}

void FVBPlayBotChecks::Look(const FString& Name, int64 FromFrame, int64 ToFrame, const FString& Note)
{
	++Looks;
	if (Emit)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"look\",\"name\":\"%s\",\"from\":%lld,\"to\":%lld,\"note\":\"%s\"}"), *Escape(Name), Stamp(FromFrame), Stamp(ToFrame), *Escape(Note)));
	}
}

void FVBPlayBotChecks::ReportUnusedForceWrong(int64 Frame)
{
	for (const FString& Name : ForceWrong)
	{
		if (!Evaluated.Contains(Name))
		{
			Unavailable(TEXT("force_wrong_unused:") + Name, Frame,
				TEXT("forced check name was never evaluated (typo or the step did not run)"));
		}
	}
}

FString FVBPlayBotChecks::StatusWord() const
{
	if (Failed > 0 || Unknown > 0)
	{
		return TEXT("fail");
	}

	if (Passed > 0)
	{
		return TEXT("pass");
	}

	return TEXT("measured");
}

void FVBPlayBotChecks::ExpectSCSComponent(AActor* Owner, UClass* Type, FName ExpectedName, const UObject* Found,
	const FString& Name, int64 Frame, bool bRequireReplicated)
{
	TArray<UActorComponent*> Components;
	if (Owner && Type)
	{
		Owner->GetComponents(Type, Components);
	}

	const UActorComponent* Component = Components.Num() == 1 ? Components[0] : nullptr;
	const bool bCorrect = Component && Component == Found && Component->GetFName() == ExpectedName
		&& Component->IsRegistered() && Component->IsCreatedByConstructionScript()
		&& !Component->HasAnyFlags(RF_DefaultSubObject) && Component->IsNameStableForNetworking()
		&& (!bRequireReplicated || Component->GetIsReplicated());
	Expect(Name, bCorrect, true, Frame,
		FString::Printf(TEXT("{\"count\":%d,\"found\":\"%s\"}"), Components.Num(), *GetPathNameSafe(Found).ReplaceCharWithEscapedChar()));
}
