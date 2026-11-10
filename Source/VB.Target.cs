// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class VBTarget : TargetRules
{
	public VBTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		ExtraModuleNames.Add("VB");
		ApplySharedVBTargetSettings(this);
	}

	// 모든 VB Target 이 같은 빌드 설정을 쓰는 한 곳 (Lyra LyraGameTarget.ApplySharedLyraTargetSettings 와 같은 자리).
	//   Target 마다 따로 적으면 엔진을 올릴 때 한쪽만 바뀐다 - 5.8 로 옮긴 뒤에도 include 순서가 두 파일 모두 5.7 에 남아 있었다.
	//   Server · Client Target 은 설치판 엔진이 빌드하지 못한다(Engine/Config/BaseEngine.ini 의 InstalledPlatformConfigurations 에
	//   Editor · Game 만 있다). 소스 엔진으로 옮길 때 그 Target 들도 이 함수를 부른다(Docs/Implementation/CodeAudit/Design_L10_BuildPackaging.md 6.1).
	internal static void ApplySharedVBTargetSettings(TargetRules Target)
	{
		Target.DefaultBuildSettings = BuildSettingsVersion.V7;
		Target.IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
	}
}
