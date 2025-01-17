/**
\author		Shun Moriya
\copyright	2023- Shun Moriya
All Rights Reserved.
*/

#include "DungeonGeneratorCore.h"
#include "DungeonGenerateParameter.h"
#include "DungeonDoor.h"
#include "DungeonLevelStreamingDynamic.h"
#include "DungeonRoomProps.h"
#include "DungeonRoomSensor.h"
#include "Core/Debug/Debug.h"
#include "Core/Debug/BuildInfomation.h"
#include "Core/Identifier.h"
#include "Core/Generator.h"
#include "Core/Voxel.h"
#include "Core/Math/Math.h"
#include <TextureResource.h>
#include <GameFramework/PlayerStart.h>
#include <Engine/LevelStreamingDynamic.h>
#include <Engine/StaticMeshActor.h>
#include <Engine/Texture2D.h>
#include <Kismet/GameplayStatics.h>
#include <Misc/EngineVersionComparison.h>
#include <NavMesh/NavMeshBoundsVolume.h>
#include <NavMesh/RecastNavMesh.h>

#include <Components/BrushComponent.h>
#include <Engine/Polys.h>

#if WITH_EDITOR
// UnrealEd
#include <EditorLevelUtils.h>
#include <Builders/CubeBuilder.h>
#endif

// 定義するとミッショングラフのデバッグファイル(PlantUML)を出力します
#define DEBUG_GENERATE_MISSION_GRAPH_FILE

static const FName DungeonGeneratorTag(TEXT("DungeonGenerator"));

namespace
{
	FTransform GetWorldTransform_(const float yaw, const FVector& position)
	{
		return FTransform(FRotator(0.f, yaw, 0.f).Quaternion(), position);
	}
}

const FName& CDungeonGeneratorCore::GetDungeonGeneratorTag()
{
	return DungeonGeneratorTag;
}

CDungeonGeneratorCore::CDungeonGeneratorCore(const TWeakObjectPtr<UWorld>& world)
	: mWorld(world)
{
	AddStaticMeshEvent addStaticMeshEvent = [this](UStaticMesh* staticMesh, const FTransform& transform)
	{
		SpawnStaticMeshActor(staticMesh, TEXT("Dungeon/Meshes"), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	};
	AddPillarStaticMeshEvent addPillarStaticMeshEvent = [this](uint32_t gridHeight, UStaticMesh* staticMesh, const FTransform& transform)
	{
		SpawnStaticMeshActor(staticMesh, TEXT("Dungeon/Meshes"), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	};

	mOnAddFloor = addStaticMeshEvent;
	mOnAddSlope = addStaticMeshEvent;
	mOnAddWall = addStaticMeshEvent;
	mOnAddRoomRoof = addStaticMeshEvent;
	mOnAddAisleRoof = addStaticMeshEvent;
	mOnResetPillar = addPillarStaticMeshEvent;
}

bool CDungeonGeneratorCore::Create(const UDungeonGenerateParameter* parameter)
{
	// Conversion from UDungeonGenerateParameter to dungeon::GenerateParameter
	if (!IsValid(parameter))
	{
		DUNGEON_GENERATOR_ERROR(TEXT("Set the dungeon generation parameters"));
		Clear();
	}

	dungeon::GenerateParameter generateParameter;
	int32 randomSeed = parameter->GetRandomSeed();
	if (parameter->GetRandomSeed() == 0)
		randomSeed = static_cast<int32>(time(nullptr));
	generateParameter.mRandom.SetSeed(randomSeed);
	const_cast<UDungeonGenerateParameter*>(parameter)->SetGeneratedRandomSeed(randomSeed);
	generateParameter.mNumberOfCandidateFloors = parameter->NumberOfCandidateFloors;
	generateParameter.mNumberOfCandidateRooms = parameter->NumberOfCandidateRooms;
	generateParameter.mMinRoomWidth = parameter->RoomWidth.Min;
	generateParameter.mMaxRoomWidth = parameter->RoomWidth.Max;
	generateParameter.mMinRoomDepth = parameter->RoomDepth.Min;
	generateParameter.mMaxRoomDepth = parameter->RoomDepth.Max;
	generateParameter.mMinRoomHeight = parameter->RoomHeight.Min;
	generateParameter.mMaxRoomHeight = parameter->RoomHeight.Max;
	generateParameter.mHorizontalRoomMargin = parameter->RoomMargin;
	generateParameter.mVerticalRoomMargin = parameter->VerticalRoomMargin;
	mParameter = parameter;

	mGenerator = std::make_shared<dungeon::Generator>();
	mGenerator->OnQueryParts([this, parameter](const std::shared_ptr<dungeon::Room>& room)
	{
		CreateImpl_AddRoomAsset(parameter, room);
	});
	mGenerator->Generate(generateParameter);

	// デバッグ情報を出力
#if defined(DEBUG_GENERATE_MISSION_GRAPH_FILE)
	{
		// TODO:外部からファイル名を与えられるように変更して下さい
		const FString path = FPaths::ProjectSavedDir() + TEXT("/dungeon_diagram.pu");
		mGenerator->DumpRoomDiagram(TCHAR_TO_UTF8(*path));
	}
#endif

	// 生成エラーを確認する
	dungeon::Generator::Error generatorError = mGenerator->GetLastError();
	if (dungeon::Generator::Error::Success != generatorError)
	{
		DUNGEON_GENERATOR_LOG(TEXT("Found error."));

#if WITH_EDITOR
		// デバッグに必要な情報（デバッグ生成パラメータ）を出力する
		/*
		TODO:外部からファイル名を与えられるように変更して下さい
		const FString path = FPaths::ProjectSavedDir() + TEXT("/dungeon_diagram.json");
		*/
		parameter->DumpToJson();
#endif

		return false;
	}
	else
	{
		AddTerrain();
		AddObject();

		DUNGEON_GENERATOR_LOG(TEXT("Done."));
		return true;
	}
}

bool CDungeonGeneratorCore::CreateImpl_AddRoomAsset(const UDungeonGenerateParameter* parameter, const std::shared_ptr<dungeon::Room>& room)
{
	parameter->EachDungeonRoomLocator([this, parameter, &room](const FDungeonRoomLocator& dungeonRoomLocator)
	{
		if (dungeonRoomLocator.GetDungeonParts() != EDungeonRoomParts::Any)
		{
			// Match the order of EDungeonRoomParts and dungeon::Room::Parts
			if (static_cast<dungeon::Room::Parts>(dungeonRoomLocator.GetDungeonParts()) != room->GetParts())
				return;
		}

		switch (dungeonRoomLocator.GetWidthCondition())
		{
		case EDungeonRoomSizeCondition::Equal:
			if (room->GetWidth() != dungeonRoomLocator.GetWidth())
				return;
			break;

		case EDungeonRoomSizeCondition::EqualGreater:
			if (room->GetWidth() < dungeonRoomLocator.GetWidth())
				return;
			break;
		}

		switch (dungeonRoomLocator.GetDepthCondition())
		{
		case EDungeonRoomSizeCondition::Equal:
			if (room->GetDepth() != dungeonRoomLocator.GetDepth())
				return;
			break;

		case EDungeonRoomSizeCondition::EqualGreater:
			if (room->GetDepth() < dungeonRoomLocator.GetDepth())
				return;
			break;
		}

		switch (dungeonRoomLocator.GetHeightCondition())
		{
		case EDungeonRoomSizeCondition::Equal:
			if (room->GetHeight() != dungeonRoomLocator.GetHeight())
				return;
			break;

		case EDungeonRoomSizeCondition::EqualGreater:
			if (room->GetHeight() < dungeonRoomLocator.GetHeight())
				return;
			break;
		}

		if (!IsStreamLevelRequested(dungeonRoomLocator.GetLevelPath()))
		{
			room->SetDataSize(dungeonRoomLocator.GetWidth(), dungeonRoomLocator.GetDepth(), dungeonRoomLocator.GetHeight());

			FIntVector min, max;
			room->GetDataBounds(min, max);
			room->SetNoMeshGeneration(!dungeonRoomLocator.IsGenerateRoofMesh(), !dungeonRoomLocator.IsGenerateFloorMesh());

			const float halfGridSize = parameter->GetGridSize() * 0.5f;
			const FVector halfOffset(halfGridSize, halfGridSize, 0);

			RequestStreamLevel(dungeonRoomLocator.GetLevelPath(), FVector(min) * parameter->GetGridSize() + halfOffset);
		}
	});

	return true;
}

void CDungeonGeneratorCore::AddTerrain()
{
	if (mGenerator == nullptr)
	{
		DUNGEON_GENERATOR_ERROR(TEXT("CDungeonGeneratorCore::Createを呼び出してください"));
		return;
	}

	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (!IsValid(parameter))
	{
		DUNGEON_GENERATOR_ERROR(TEXT("DungeonGenerateParameterを設定してください"));
		return;
	}

	mGenerator->GetVoxel()->Each([this, parameter](const FIntVector& location, const dungeon::Grid& grid)
		{
			const size_t gridIndex = mGenerator->GetVoxel()->Index(location);
			const float gridSize = parameter->GetGridSize();
			const float halfGridSize = gridSize * 0.5f;
			const FVector halfOffset(halfGridSize, halfGridSize, 0);
			const FVector position = parameter->ToWorld(location);
			const FVector centerPosition = position + halfOffset;

			if (mOnAddSlope && grid.CanBuildSlope())
			{
				/*
				スロープのメッシュを生成
				メッシュは原点からX軸とY軸方向に伸びており、面はZ軸が上面になっています。
				*/
				if (const FDungeonMeshParts* parts = parameter->SelectSlopeParts(gridIndex, grid, dungeon::Random::Instance()))
				{
					mOnAddSlope(parts->StaticMesh, parts->CalculateWorldTransform(centerPosition, grid.GetDirection()));
				}
			}
			else if (mOnAddFloor && grid.CanBuildFloor(mGenerator->GetVoxel()->Get(location.X, location.Y, location.Z - 1), true))
			{
				/*
				床のメッシュを生成
				メッシュは原点からX軸とY軸方向に伸びており、面はZ軸が上面になっています。
				*/
				if (const FDungeonMeshParts* parts = parameter->SelectFloorParts(gridIndex, grid, dungeon::Random::Instance()))
				{
					mOnAddFloor(parts->StaticMesh, parts->CalculateWorldTransform(centerPosition, grid.GetDirection()));
				}
			}

			/*
			壁のメッシュを生成
			メッシュは原点からY軸とZ軸方向に伸びており、面はX軸が正面（北側の壁）になっています。
			*/
			if (mOnAddWall)
			{
				if (const FDungeonMeshParts* parts = parameter->SelectWallParts(gridIndex, grid, dungeon::Random::Instance()))
				{
					if (grid.CanBuildWall(mGenerator->GetVoxel()->Get(location.X, location.Y - 1, location.Z), dungeon::Direction::North, parameter->MergeRooms))
					{
						// 北側の壁
						FVector wallPosition = centerPosition;
						wallPosition.Y -= halfGridSize;
						mOnAddWall(parts->StaticMesh, parts->CalculateWorldTransform(wallPosition, 0.f));
					}
					if (grid.CanBuildWall(mGenerator->GetVoxel()->Get(location.X, location.Y + 1, location.Z), dungeon::Direction::South, parameter->MergeRooms))
					{
						// 南側の壁
						FVector wallPosition = centerPosition;
						wallPosition.Y += halfGridSize;
						mOnAddWall(parts->StaticMesh, parts->CalculateWorldTransform(wallPosition, 180.f));
					}
					if (grid.CanBuildWall(mGenerator->GetVoxel()->Get(location.X + 1, location.Y, location.Z), dungeon::Direction::East, parameter->MergeRooms))
					{
						// 東側の壁
						FVector wallPosition = centerPosition;
						wallPosition.X += halfGridSize;
						mOnAddWall(parts->StaticMesh, parts->CalculateWorldTransform(wallPosition, 90.f));
					}
					if (grid.CanBuildWall(mGenerator->GetVoxel()->Get(location.X - 1, location.Y, location.Z), dungeon::Direction::West, parameter->MergeRooms))
					{
						// 西側の壁
						FVector wallPosition = centerPosition;
						wallPosition.X -= halfGridSize;
						mOnAddWall(parts->StaticMesh, parts->CalculateWorldTransform(wallPosition, -90.f));
					}
				}
			}

			/*
			柱のメッシュを生成
			メッシュは原点からY軸とZ軸方向に伸びており、面はX軸が正面になっています。
			*/
			if (mOnResetPillar)
			{
				FVector wallVector(0.f);
				uint8_t wallCount = 0;
				bool onFloor = false;
				uint32_t pillarGridHeight = 1;
				for (int_fast8_t dy = -1; dy <= 0; ++dy)
				{
					for (int_fast8_t dx = -1; dx <= 0; ++dx)
					{
						// 壁の数を調べます
						const auto& result = mGenerator->GetVoxel()->Get(location.X + dx, location.Y + dy, location.Z);
						if (grid.CanBuildPillar(result))
						{
							wallVector += FVector(static_cast<float>(dx) + 0.5f, static_cast<float>(dy) + 0.5f, 0.f);
							++wallCount;
						}

						// 床を調べます
						const auto& baseFloorGrid = mGenerator->GetVoxel()->Get(location.X + dx, location.Y + dy, location.Z);
						const auto& underFloorGrid = mGenerator->GetVoxel()->Get(location.X + dx, location.Y + dy, location.Z - 1);
						if (baseFloorGrid.CanBuildSlope() || baseFloorGrid.CanBuildFloor(underFloorGrid, false))
						{
							onFloor = true;

							// 天井の高さを調べます
							uint32_t gridHeight = 1;
							while (true)
							{
								const auto& roofGrid = mGenerator->GetVoxel()->Get(location.X + dx, location.Y + dy, location.Z + gridHeight);
								if (roofGrid.GetType() == dungeon::Grid::Type::OutOfBounds)
									break;
								if (!grid.CanBuildRoof(roofGrid, false))
									break;
								++gridHeight;
							}
							if (pillarGridHeight < gridHeight)
								pillarGridHeight = gridHeight;
						}
					}
				}
				if (onFloor && 0 < wallCount && wallCount < 4)
				{
					wallVector.Normalize();

					const FTransform transform(wallVector.Rotation(), position);
					if (const FDungeonMeshParts* parts = parameter->SelectPillarParts(gridIndex, grid, dungeon::Random::Instance()))
					{
						mOnResetPillar(pillarGridHeight, parts->StaticMesh, parts->CalculateWorldTransform(transform));
					}

					// 水平以外に対応が必要？
					if (wallCount == 2)
					{
						if (const FDungeonActorParts* parts = parameter->SelectTorchParts(gridIndex, grid, dungeon::Random::Instance()))
						{
#if 0
							const FTransform worldTransform = transform * parts->RelativeTransform;
							//const FTransform worldTransform = parts->RelativeTransform * transform;
#else
							const FVector rotaedLocation = transform.Rotator().RotateVector(parts->RelativeTransform.GetLocation());
							const FTransform worldTransform(
								transform.Rotator() + parts->RelativeTransform.Rotator(),
								transform.GetLocation() + rotaedLocation,
								transform.GetScale3D() * parts->RelativeTransform.GetScale3D()
							);
#endif
							SpawnActor(parts->ActorClass, TEXT("Dungeon/Actors"), worldTransform);
						}
					}
				}
			}

			// 扉の生成通知
			if (const FDungeonDoorActorParts* parts = parameter->SelectDoorParts(gridIndex, grid, dungeon::Random::Instance()))
			{
				const EDungeonRoomProps props = static_cast<EDungeonRoomProps>(grid.GetProps());

				if (grid.CanBuildGate(mGenerator->GetVoxel()->Get(location.X, location.Y - 1, location.Z), dungeon::Direction::North))
				{
					// 北側の扉
					FVector doorPosition = position;
					doorPosition.X += parameter->GridSize * 0.5f;
					SpawnDoorActor(parts->ActorClass, parts->CalculateWorldTransform(doorPosition, 0.f), props);
				}
				if (grid.CanBuildGate(mGenerator->GetVoxel()->Get(location.X, location.Y + 1, location.Z), dungeon::Direction::South))
				{
					// 南側の扉
					FVector doorPosition = position;
					doorPosition.X += parameter->GridSize * 0.5f;
					doorPosition.Y += parameter->GridSize;
					SpawnDoorActor(parts->ActorClass, parts->CalculateWorldTransform(doorPosition, 180.f), props);
				}
				if (grid.CanBuildGate(mGenerator->GetVoxel()->Get(location.X + 1, location.Y, location.Z), dungeon::Direction::East))
				{
					// 東側の扉
					FVector doorPosition = position;
					doorPosition.X += parameter->GridSize;
					doorPosition.Y += parameter->GridSize * 0.5f;
					SpawnDoorActor(parts->ActorClass, parts->CalculateWorldTransform(doorPosition, 90.f), props);
				}
				if (grid.CanBuildGate(mGenerator->GetVoxel()->Get(location.X - 1, location.Y, location.Z), dungeon::Direction::West))
				{
					// 西側の扉
					FVector doorPosition = position;
					doorPosition.Y += parameter->GridSize * 0.5f;
					SpawnDoorActor(parts->ActorClass, parts->CalculateWorldTransform(doorPosition, -90.f), props);
				}
			}

			// 屋根のメッシュ生成通知
			if (grid.CanBuildRoof(mGenerator->GetVoxel()->Get(location.X, location.Y, location.Z + 1), true))
			{
				/*
				壁のメッシュを生成
				メッシュは原点からY軸とZ軸方向に伸びており、面はX軸が正面になっています。
				*/
				const FTransform transform(centerPosition);
				//if (grid.CanBuildWall(mGenerator->GetVoxel()->Get(location.X, location.Y - 1, location.Z), dungeon::Direction::North, parameter->MergeRooms))
				if (grid.IsKindOfRoomType())
				{
					if (mOnAddRoomRoof)
					{
						if (const FDungeonMeshPartsWithDirection* parts = parameter->SelectRoomRoofParts(gridIndex, grid, dungeon::Random::Instance()))
						{
							mOnAddRoomRoof(
								parts->StaticMesh,
								parts->CalculateWorldTransform(dungeon::Random::Instance(), transform)
							);
						}
					}
				}
				else
				{
					if (mOnAddAisleRoof)
					{
						if (const FDungeonMeshPartsWithDirection* parts = parameter->SelectAisleRoofParts(gridIndex, grid, dungeon::Random::Instance()))
						{
							mOnAddAisleRoof(
								parts->StaticMesh,
								parts->CalculateWorldTransform(dungeon::Random::Instance(), transform)
							);
						}
					}
				}

#if 0
				if (mOnResetChandelier)
				{
					if (const FDungeonActorParts* parts = parameter->SelectChandelierParts(dungeon::Random::Instance()))
					{
						mOnResetChandelier(parts->ActorClass, worldTransform);
					}
				}
#endif
			}

			return true;
		}
	);

	// RoomSensorActorを生成
	mGenerator->ForEach([this, parameter](const std::shared_ptr<const dungeon::Room>& room)
		{
			const FVector center = room->GetCenter() * parameter->GetGridSize();
			const FVector extent = room->GetExtent() * parameter->GetGridSize();
			SpawnRoomSensorActor(
				parameter->GetRoomSensorClass(),
				room->GetIdentifier(),
				center,
				extent,
				static_cast<EDungeonRoomParts>(room->GetParts()),
				static_cast<EDungeonRoomItem>(room->GetItem()),
				room->GetBranchId(),
				room->GetDepthFromStart(),
				mGenerator->GetDeepestDepthFromStart()	//!< TODO:適切な関数名に変えて下さい
			);
		}
	);

	SpawnRecastNavMesh();
#if 0
	//SpawnRecastNavMesh();
	SpawnNavMeshBoundsVolume(parameter);

#else
	if (ANavMeshBoundsVolume* navMeshBoundsVolume = FindActor<ANavMeshBoundsVolume>())
	{
		const FBox& bounding = CalculateBoundingBox();
		const FVector& boundingCenter = bounding.GetCenter();
		const FVector& boundingExtent = bounding.GetExtent();

		if (USceneComponent* rootComponent = navMeshBoundsVolume->GetRootComponent())
		{
			rootComponent->SetMobility(EComponentMobility::Stationary);

			navMeshBoundsVolume->SetActorLocation(boundingCenter);
			navMeshBoundsVolume->SetActorScale3D(FVector::OneVector);

#if WITH_EDITOR
			// ブラシビルダーを生成 (UnrealEd)
			if (UCubeBuilder* cubeBuilder = NewObject<UCubeBuilder>())
			{
				cubeBuilder->X = boundingExtent.X * 2.0;
				cubeBuilder->Y = boundingExtent.Y * 2.0;
				cubeBuilder->Z = boundingExtent.Z * 2.0;

				// ブラシ生成開始
				navMeshBoundsVolume->PreEditChange(nullptr);

				const EObjectFlags objectFlags = navMeshBoundsVolume->GetFlags() & (RF_Transient | RF_Transactional);
				navMeshBoundsVolume->Brush = NewObject<UModel>(navMeshBoundsVolume, NAME_None, objectFlags);
				navMeshBoundsVolume->Brush->Initialize(nullptr, true);
				navMeshBoundsVolume->Brush->Polys = NewObject<UPolys>(navMeshBoundsVolume->Brush, NAME_None, objectFlags);
				navMeshBoundsVolume->GetBrushComponent()->Brush = navMeshBoundsVolume->Brush;
				navMeshBoundsVolume->BrushBuilder = DuplicateObject<UBrushBuilder>(cubeBuilder, navMeshBoundsVolume);

				// ブラシビルダーを使ってブラシを生成
				cubeBuilder->Build(navMeshBoundsVolume->GetWorld(), navMeshBoundsVolume);

				// ブラシ生成終了
				navMeshBoundsVolume->PostEditChange();

				// 登録
				navMeshBoundsVolume->PostRegisterAllComponents();
			}
			else
			{
				DUNGEON_GENERATOR_ERROR(TEXT("CDungeonGeneratorCore: CubeBuilderの生成に失敗しました"));
			}
#else
			/*
			UCubeBuilderはエディタでのみ使用可能なので
			スケールによるサイズの変更をスケールで代用します。
			*/
			const FBoxSphereBounds boxSphereBounds = navMeshBoundsVolume->GetBounds();
			const FVector boundingScale = boundingExtent / boxSphereBounds.BoxExtent;
			navMeshBoundsVolume->SetActorScale3D(boundingScale);
#endif

			rootComponent->SetMobility(EComponentMobility::Static);
		}
		else
		{
			DUNGEON_GENERATOR_ERROR(TEXT("NavMeshBoundsVolumeのRootComponentを設定して下さい"));
		}
	}
#endif
}

void CDungeonGeneratorCore::SpawnRecastNavMesh()
{
	if (ARecastNavMesh* navMeshBoundsVolume = FindActor<ARecastNavMesh>())
	{
		const auto mode = navMeshBoundsVolume->GetRuntimeGenerationMode();
		if (mode != ERuntimeGenerationType::Dynamic && mode != ERuntimeGenerationType::DynamicModifiersOnly)
		{
			DUNGEON_GENERATOR_ERROR(TEXT("RecastNavMeshのRuntimeGenerationModeをDynamicに設定してください"));
		}
	}
}

void CDungeonGeneratorCore::AddObject()
{
	if (mGenerator == nullptr)
	{
		DUNGEON_GENERATOR_ERROR(TEXT("CDungeonGeneratorCore::Createを呼び出してください"));
		return;
	}

	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (!IsValid(parameter))
	{
		DUNGEON_GENERATOR_ERROR(TEXT("DungeonGenerateParameterを設定してください"));
		return;
	}

	if (parameter->GetStartParts().ActorClass)
	{
		/**
		TODO:パーツによる回転指定 PlacementDirection に対応して下さい
		*/
		const FTransform wallTransform(*mGenerator->GetStartPoint() * parameter->GetGridSize());
		const FTransform worldTransform = wallTransform * parameter->GetStartParts().RelativeTransform;
		SpawnActorOnFloor(parameter->GetStartParts().ActorClass, worldTransform);
	}

	if (parameter->GetGoalParts().ActorClass)
	{
		/**
		TODO:パーツによる回転指定 PlacementDirection に対応して下さい
		*/
		const FTransform wallTransform(*mGenerator->GetGoalPoint() * parameter->GetGridSize());
		const FTransform worldTransform = wallTransform * parameter->GetGoalParts().RelativeTransform;
		SpawnActorOnFloor(parameter->GetGoalParts().ActorClass, worldTransform);
	}
}

void CDungeonGeneratorCore::Clear()
{
	mGenerator.reset();
	mParameter = nullptr;
}

FTransform CDungeonGeneratorCore::GetStartTransform() const
{
	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (IsValid(parameter) && mGenerator != nullptr && mGenerator->GetLastError() == dungeon::Generator::Error::Success)
	{
		const FTransform wallTransform(*mGenerator->GetStartPoint() * parameter->GetGridSize());
		const FTransform worldTransform = wallTransform * parameter->GetStartParts().RelativeTransform;
		return worldTransform;
	}
	return FTransform::Identity;
}

FTransform CDungeonGeneratorCore::GetGoalTransform() const
{
	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (IsValid(parameter) && mGenerator != nullptr && mGenerator->GetLastError() == dungeon::Generator::Error::Success)
	{
		const FTransform wallTransform(*mGenerator->GetGoalPoint() * parameter->GetGridSize());
		const FTransform worldTransform = wallTransform * parameter->GetGoalParts().RelativeTransform;
		return worldTransform;
	}
	return FTransform::Identity;
}

FVector CDungeonGeneratorCore::GetStartLocation() const
{
	return GetStartTransform().GetLocation();
}

FVector CDungeonGeneratorCore::GetGoalLocation() const
{
	return GetGoalTransform().GetLocation();
}

/**
2D空間は（X軸:前 Y軸:右）
3D空間は（X軸:前 Y軸:右 Z軸:上）である事に注意
*/
static inline FBox ToWorldBoundingBox(const UDungeonGenerateParameter* parameter, const std::shared_ptr<const dungeon::Room>& room)
{
	check(parameter);
	const FVector min = parameter->ToWorld(room->GetLeft(), room->GetTop(), room->GetBackground());
	const FVector max = parameter->ToWorld(room->GetRight(), room->GetBottom(), room->GetForeground());
	return FBox(min, max);
}

FBox CDungeonGeneratorCore::CalculateBoundingBox() const
{
	if (mGenerator)
	{
		const UDungeonGenerateParameter* parameter = mParameter.Get();
		if (IsValid(parameter))
		{
			FBox boundingBox(EForceInit::ForceInitToZero);
			mGenerator->ForEach([&boundingBox, parameter](const std::shared_ptr<const dungeon::Room>& room)
				{
					boundingBox += ToWorldBoundingBox(parameter, room);
				}
			);
			boundingBox.Min.Z -= parameter->GetGridSize();
			boundingBox.Max.Z += parameter->GetGridSize();
			return boundingBox;
		}
	}

	return FBox(EForceInit::ForceInitToZero);
}

void CDungeonGeneratorCore::MovePlayerStart()
{
	if (APlayerStart* playerStart = FindActor<APlayerStart>())
	{
		// APlayerStartはコリジョンが無効になっているので、GetSimpleCollisionCylinderを利用する事ができない
		if (USceneComponent* rootComponent = playerStart->GetRootComponent())
		{
			// 接地しない様に少しだけ余白を作る
			static constexpr float heightMargine = 10.f;

			const EComponentMobility::Type mobility = rootComponent->Mobility;
			rootComponent->SetMobility(EComponentMobility::Movable);
			{
				float cylinderRadius, cylinderHalfHeight;
				rootComponent->CalcBoundingCylinder(cylinderRadius, cylinderHalfHeight);

				FVector location = GetStartLocation();

				const UDungeonGenerateParameter* parameter = mParameter.Get();
				const auto offsetZ = IsValid(parameter) ? parameter->GetGridSize() : (cylinderHalfHeight * 2);
				FHitResult hitResult;
				if (playerStart->GetWorld()->LineTraceSingleByChannel(hitResult, location + FVector(0, 0, offsetZ), location, ECollisionChannel::ECC_Pawn))
				{
					location = hitResult.ImpactPoint;
				}
				location.Z += cylinderHalfHeight + heightMargine;

				playerStart->SetActorLocation(location);

				FCollisionShape collisionShape;
#if UE_VERSION_OLDER_THAN(5, 0, 0)
				collisionShape.SetBox(FVector(cylinderRadius, cylinderRadius, cylinderHalfHeight));
#else
				collisionShape.SetBox(FVector3f(cylinderRadius, cylinderRadius, cylinderHalfHeight));
#endif
				if (playerStart->GetWorld()->OverlapBlockingTestByChannel(location, playerStart->GetActorQuat(), ECollisionChannel::ECC_Pawn, collisionShape))
				{
					DUNGEON_GENERATOR_ERROR(TEXT("%s(PlayerStart)が何かに接触しています"), *playerStart->GetName());
				}
			}
			rootComponent->SetMobility(mobility);
		}
		else
		{
			DUNGEON_GENERATOR_ERROR(TEXT("%s(PlayerStart)のRootComponentを設定して下さい"), *playerStart->GetName());
		}
	}
	else
	{
		DUNGEON_GENERATOR_WARNING(TEXT("PlayerStartは発見できませんでした"));
	}
}

////////////////////////////////////////////////////////////////////////////////
AActor* CDungeonGeneratorCore::SpawnActor(UClass* actorClass, const FName& folderPath, const FTransform& transform, const ESpawnActorCollisionHandlingMethod spawnActorCollisionHandlingMethod) const
{
	AActor* actor = SpawnActorDeferred<AActor>(actorClass, folderPath, transform, spawnActorCollisionHandlingMethod);
	//ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	//ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding
	//ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
	if (!IsValid(actor))
		return nullptr;
	actor->FinishSpawning(transform);
	return actor;
}

AStaticMeshActor* CDungeonGeneratorCore::SpawnStaticMeshActor(UStaticMesh* staticMesh, const FName& folderPath, const FTransform& transform, const ESpawnActorCollisionHandlingMethod spawnActorCollisionHandlingMethod) const
{
	AStaticMeshActor* actor = SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), folderPath, transform, spawnActorCollisionHandlingMethod);
	if (!IsValid(actor))
		return nullptr;

	UStaticMeshComponent* mesh = actor->GetStaticMeshComponent();
	if (IsValid(mesh))
		mesh->SetStaticMesh(staticMesh);

	actor->FinishSpawning(transform);
	return actor;
}

void CDungeonGeneratorCore::SpawnActorOnFloor(UClass* actorClass, const FTransform& transform) const
{
	AActor* actor = SpawnActor(actorClass, TEXT("Dungeon/Actors"), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (IsValid(actor))
	{
		FVector location = actor->GetActorLocation();
		location.Z += actor->GetSimpleCollisionHalfHeight();
		actor->SetActorLocation(location);
	}
}

void CDungeonGeneratorCore::SpawnDoorActor(UClass* actorClass, const FTransform& transform, EDungeonRoomProps props) const
{
	ADungeonDoor* actor = SpawnActorDeferred<ADungeonDoor>(actorClass, TEXT("Dungeon/Actors"), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (IsValid(actor))
	{
		actor->Initialize(props);

		if (mOnResetDoor)
		{
			mOnResetDoor(actor, props);
		}
	}
	if (IsValid(actor))
	{
		actor->FinishSpawning(transform);
	}
}

void CDungeonGeneratorCore::SpawnRoomSensorActor(
	UClass* actorClass,
	const dungeon::Identifier& identifier,
	const FVector& center,
	const FVector& extent,
	EDungeonRoomParts parts,
	EDungeonRoomItem item,
	uint8 branchId,
	const uint8 depthFromStart,
	const uint8 deepestDepthFromStart) const
{
	const FTransform transform(center);
	ADungeonRoomSensor* actor = SpawnActorDeferred<ADungeonRoomSensor>(actorClass, TEXT("Dungeon/Sensors"), transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (IsValid(actor))
	{
		actor->Initialize(identifier.Get(), extent, parts, item, branchId, depthFromStart, deepestDepthFromStart);
	}
	if (IsValid(actor))
	{
		actor->FinishSpawning(transform);
	}
};

void CDungeonGeneratorCore::DestroySpawnedActors() const
{
	DestroySpawnedActors(mWorld.Get());
}

void CDungeonGeneratorCore::DestroySpawnedActors(UWorld* world)
{
	if (!IsValid(world))
		return;

	TArray<AActor*> actors;
	UGameplayStatics::GetAllActorsWithTag(world, DungeonGeneratorTag, actors);
	for (AActor* actor : actors)
	{
		if (!IsValid(actor))
			continue;

		if (ADungeonRoomSensor* dungeonRoomSensor = Cast<ADungeonRoomSensor>(actor))
		{
			dungeonRoomSensor->Finalize();
		}
		actor->Destroy();
	}
}

////////////////////////////////////////////////////////////////////////////////
UTexture2D* CDungeonGeneratorCore::GenerateMiniMapTextureWithSize(uint32_t& worldToTextureScale, uint32_t textureWidthHeight, uint32_t currentLevel) const
{
	if (mGenerator->GetLastError() != dungeon::Generator::Error::Success)
		return nullptr;

	std::shared_ptr<dungeon::Voxel> voxel = mGenerator->GetVoxel();
	if (voxel == nullptr)
		return nullptr;

	uint32_t length = 1;
	if (length < voxel->GetWidth())
		length = voxel->GetWidth();
	if (length < voxel->GetDepth())
		length = voxel->GetDepth();
	worldToTextureScale = static_cast<uint32_t>(static_cast<float>(textureWidthHeight) / static_cast<float>(length));

	return GenerateMiniMapTexture(worldToTextureScale, textureWidthHeight, currentLevel);
}

UTexture2D* CDungeonGeneratorCore::GenerateMiniMapTextureWithScale(uint32_t& worldToTextureScale, uint32_t dotScale, uint32_t currentLevel) const
{
	if (mGenerator->GetLastError() != dungeon::Generator::Error::Success)
		return nullptr;

	std::shared_ptr<dungeon::Voxel> voxel = mGenerator->GetVoxel();
	if (voxel == nullptr)
		return nullptr;

	uint32_t length = 1;
	if (length < voxel->GetWidth())
		length = voxel->GetWidth();
	if (length < voxel->GetDepth())
		length = voxel->GetDepth();
	const uint32_t textureWidthHeight = length * dotScale;
	worldToTextureScale = static_cast<uint32_t>(static_cast<float>(textureWidthHeight) / static_cast<float>(length));

	return GenerateMiniMapTexture(worldToTextureScale, textureWidthHeight, currentLevel);
}

UTexture2D* CDungeonGeneratorCore::GenerateMiniMapTexture(uint32_t worldToTextureScale, uint32_t textureWidthHeight, uint32_t currentLevel) const
{
	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (!IsValid(parameter))
		return nullptr;

	const size_t totalBufferSize = textureWidthHeight * textureWidthHeight;
	auto pixels = std::make_unique<uint8_t[]>(totalBufferSize);
	std::memset(pixels.get(), 0x00, totalBufferSize);

	std::shared_ptr<dungeon::Voxel> voxel = mGenerator->GetVoxel();

	if (currentLevel > voxel->GetHeight() - 1)
		currentLevel = voxel->GetHeight() - 1;

	auto rect = [&pixels, textureWidthHeight, worldToTextureScale](const uint32_t x, const uint32_t y, const uint8_t color) -> void
	{
		const uint32_t px = x * worldToTextureScale;
		const uint32_t py = y * worldToTextureScale;
		for (uint32_t oy = py; oy < py + worldToTextureScale; ++oy)
		{
			for (uint32_t ox = px; ox < px + worldToTextureScale; ++ox)
				pixels[textureWidthHeight * oy + ox] = color;
		}
	};

	auto line = [&pixels, textureWidthHeight, worldToTextureScale](const uint32_t x, const uint32_t y, const dungeon::Direction::Index dir, const uint8_t color) -> void
	{
		uint32_t px = x * worldToTextureScale;
		uint32_t py = y * worldToTextureScale;
		switch (dir)
		{
		case dungeon::Direction::North:
			for (uint32_t ox = px; ox < px + worldToTextureScale; ++ox)
				pixels[textureWidthHeight * py + ox] = color;
			break;

		case dungeon::Direction::South:
			py += worldToTextureScale;
			for (uint32_t ox = px; ox < px + worldToTextureScale; ++ox)
				pixels[textureWidthHeight * py + ox] = color;
			break;

		case dungeon::Direction::East:
			px += worldToTextureScale;
			for (uint32_t oy = py; oy < py + worldToTextureScale; ++oy)
				pixels[textureWidthHeight * oy + px] = color;
			break;

		case dungeon::Direction::West:
			for (uint32_t oy = py; oy < py + worldToTextureScale; ++oy)
				pixels[textureWidthHeight * oy + px] = color;
			break;

		default:
			break;
		}
	};

	// 下の階層から描画する
	const float paintRatio = 1.f / std::max(1.f, static_cast<float>(currentLevel));
	for (uint32_t z = 0; z <= currentLevel; ++z)
	{
		constexpr float floorColorRange = 0.6f;
		constexpr float wallColorRange = 0.6f;

		float floorRatio, wallRatio;
		if (currentLevel == 0)
		{
			floorRatio = 0.f + 1.f * paintRatio * floorColorRange;
			wallRatio = (1.f - wallColorRange) + 1.f * paintRatio * wallColorRange;
		}
		else
		{
			floorRatio = 0.f + static_cast<float>(z) * paintRatio * floorColorRange;
			wallRatio = (1.f - wallColorRange) + static_cast<float>(z) * paintRatio * wallColorRange;
		}
		const uint8_t floorColor = static_cast<uint8_t>(255.f * floorRatio);
		const uint8_t wallColor = static_cast<uint8_t>(255.f * wallRatio);

		for (uint32_t y = 0; y < voxel->GetDepth(); ++y)
		{
			for (uint32_t x = 0; x < voxel->GetWidth(); ++x)
			{
				const auto& grid = voxel->Get(x, y, z);
				// slope
				if (grid.CanBuildSlope() || grid.GetType() == dungeon::Grid::Type::Atrium)
				{
					rect(x, y, floorColor);
				}
				// floor
				else if (grid.CanBuildFloor(voxel->Get(x, y, z - 1), false))
				{
					rect(x, y, floorColor);
				}

				// wall
				if (grid.CanBuildWall(voxel->Get(x, y - 1, z), dungeon::Direction::North, parameter->MergeRooms))
				{
					line(x, y, dungeon::Direction::North, wallColor);
				}
				if (grid.CanBuildWall(voxel->Get(x, y + 1, z), dungeon::Direction::South, parameter->MergeRooms))
				{
					line(x, y, dungeon::Direction::South, wallColor);
				}
				if (grid.CanBuildWall(voxel->Get(x + 1, y, z), dungeon::Direction::East, parameter->MergeRooms))
				{
					line(x, y, dungeon::Direction::East, wallColor);
				}
				if (grid.CanBuildWall(voxel->Get(x - 1, y, z), dungeon::Direction::West, parameter->MergeRooms))
				{
					line(x, y, dungeon::Direction::West, wallColor);
				}
			}
		}
	}

	UTexture2D* generateTexture = UTexture2D::CreateTransient(textureWidthHeight, textureWidthHeight, PF_G8);
	generateTexture->Filter = TextureFilter::TF_Nearest;
	{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
		FTexture2DMipMap& mips = generateTexture->PlatformData->Mips[0];
#else
		FTexture2DMipMap& mips = generateTexture->GetPlatformData()->Mips[0];
#endif
		auto lockedBulkData = mips.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(lockedBulkData, pixels.get(), totalBufferSize);
		mips.BulkData.Unlock();
	}
	generateTexture->AddToRoot();
#if WITH_EDITOR
	generateTexture->Source.Init(textureWidthHeight, textureWidthHeight, 1, 1, ETextureSourceFormat::TSF_G8, pixels.get());
#endif
	generateTexture->UpdateResource();

	return generateTexture;
}

std::shared_ptr<const dungeon::Generator> CDungeonGeneratorCore::GetGenerator() const
{
	return mGenerator;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool CDungeonGeneratorCore::IsStreamLevelRequested(const FSoftObjectPath& levelPath) const
{
	const auto requestStreamLevel = std::find_if(mRequestLoadStreamLevels.begin(), mRequestLoadStreamLevels.end(), [&levelPath](const LoadStreamLevelParameter& requestStreamLevel)
		{
			return requestStreamLevel.mPath == levelPath;
		}
	);
	return requestStreamLevel != mRequestLoadStreamLevels.end();
}

void CDungeonGeneratorCore::RequestStreamLevel(const FSoftObjectPath& levelPath, const FVector& levelLocation)
{
	mRequestLoadStreamLevels.emplace_back(levelPath, levelLocation);
}

void CDungeonGeneratorCore::AsyncLoadStreamLevels()
{
	if (!mRequestLoadStreamLevels.empty())
	{
		LoadStreamLevelParameter requestStreamLevel = mRequestLoadStreamLevels.front();
		mRequestLoadStreamLevels.pop_front();

		if (FindLoadedStreamLevel(requestStreamLevel.mPath) == nullptr)
		{
			UWorld* world = mWorld.Get();
			if (IsValid(world))
			{
				bool bSuccess = false;
#if UE_VERSION_NEWER_THAN(5, 1, 0)
				const FTransform transform(FRotator::ZeroRotator, requestStreamLevel.mLocation);
				ULevelStreamingDynamic::FLoadLevelInstanceParams parameter(world, requestStreamLevel.mPath.GetLongPackageName(), transform);
				parameter.OptionalLevelStreamingClass = UDungeonLevelStreamingDynamic::StaticClass();
				ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(parameter, bSuccess);
#elif UE_VERSION_NEWER_THAN(5, 0, 0)
				ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(
					world,
					requestStreamLevel.mPath.GetLongPackageName(),
					requestStreamLevel.mLocation,
					FRotator::ZeroRotator,
					bSuccess,
					TEXT(""),
					UDungeonLevelStreamingDynamic::StaticClass(),
					false);
#else
				ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(
					world,
					requestStreamLevel.mPath.GetLongPackageName(),
					requestStreamLevel.mLocation,
					FRotator::ZeroRotator,
					bSuccess);
#endif
				if (bSuccess && IsValid(levelStreaming))
				{
					mLoadedStreamLevels.Add(levelStreaming);

					DUNGEON_GENERATOR_LOG(TEXT("Load Level (%s)"), *requestStreamLevel.mPath.GetLongPackageName());
				}
				else
				{
					DUNGEON_GENERATOR_ERROR(TEXT("Failed to Load Level (%s)"), *requestStreamLevel.mPath.GetLongPackageName());
				}
			}
		}
	}
}

#if WITH_EDITOR
void CDungeonGeneratorCore::SyncLoadStreamLevels()
{
	UWorld* world = mWorld.Get();
	if (IsValid(world))
	{
		TArray<AActor*> moveActors;

		for (const auto& requestStreamLevel : mRequestLoadStreamLevels)
		{
			if (FindLoadedStreamLevel(requestStreamLevel.mPath))
				continue;

			bool bSuccess = false;

#if UE_VERSION_NEWER_THAN(5, 1, 0)
			const FTransform transform(FRotator::ZeroRotator, requestStreamLevel.mLocation);
			ULevelStreamingDynamic::FLoadLevelInstanceParams parameter(world, requestStreamLevel.mPath.GetLongPackageName(), transform);
			parameter.OptionalLevelStreamingClass = UDungeonLevelStreamingDynamic::StaticClass();
			ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(parameter, bSuccess);
#elif UE_VERSION_NEWER_THAN(5, 0, 0)
			ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(
				world,
				requestStreamLevel.mPath.GetLongPackageName(),
				requestStreamLevel.mLocation,
				FRotator::ZeroRotator,
				bSuccess,
				TEXT(""),
				UDungeonLevelStreamingDynamic::StaticClass(),
				false);
#else
			ULevelStreamingDynamic* levelStreaming = ULevelStreamingDynamic::LoadLevelInstance(
				world,
				requestStreamLevel.mPath.GetLongPackageName(),
				requestStreamLevel.mLocation,
				FRotator::ZeroRotator,
				bSuccess);
#endif
			if (bSuccess && IsValid(levelStreaming))
			{
				levelStreaming->bShouldBlockOnLoad = true;
				world->FlushLevelStreaming();
				//world->UpdateLevelStreaming();

				ULevel* loadedLevel = levelStreaming->GetLoadedLevel();
				if (IsValid(loadedLevel))
				{
					FString folder = levelStreaming->PackageNameToLoad.ToString();
					folder.RemoveFromStart("/Game/", ESearchCase::IgnoreCase);
					folder.RemoveFromStart("Map/", ESearchCase::IgnoreCase);
					folder.RemoveFromStart("Maps/", ESearchCase::IgnoreCase);
					folder.RemoveFromStart("Level/", ESearchCase::IgnoreCase);
					folder.RemoveFromStart("Levels/", ESearchCase::IgnoreCase);

					for (AActor* actor : loadedLevel->Actors)
					{
						actor->Tags.Add(GetDungeonGeneratorTag());

						const FName folderPath(FString(TEXT("Dungeon/Levels/")) + folder);
						actor->SetFolderPath(folderPath);
					}

					moveActors.Append(loadedLevel->Actors);
				}

				mLoadedStreamLevels.Add(levelStreaming);

				DUNGEON_GENERATOR_LOG(TEXT("Load Level (%s)"), *requestStreamLevel.mPath.GetLongPackageName());
			}
			else
			{
				DUNGEON_GENERATOR_ERROR(TEXT("Failed to Load Level (%s)"), *requestStreamLevel.mPath.GetLongPackageName());
			}
		}

		if (moveActors.Num() > 0)
		{
			EditorLevelUtils::MoveActorsToLevel(moveActors, world->PersistentLevel);
		}

		mRequestLoadStreamLevels.clear();
	}
}
#endif

void CDungeonGeneratorCore::UnloadStreamLevels()
{
	mRequestLoadStreamLevels.clear();

	UWorld* world = mWorld.Get();
	if (IsValid(world))
	{
		for (const TSoftObjectPtr<ULevelStreamingDynamic>& streamLevel : mLoadedStreamLevels)
		{
			world->RemoveStreamingLevel(streamLevel.Get());
		}
		mLoadedStreamLevels.Empty();
	}
}

TSoftObjectPtr<const ULevelStreamingDynamic> CDungeonGeneratorCore::FindLoadedStreamLevel(const FSoftObjectPath& levelPath) const
{
	for (const TSoftObjectPtr<const ULevelStreamingDynamic>& loadedStreamLevel : mLoadedStreamLevels)
	{
		if (loadedStreamLevel.IsValid())
		{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
			if (loadedStreamLevel->PackageNameToLoad == levelPath.GetAssetPathName())
#else
			if (loadedStreamLevel->PackageNameToLoad == levelPath.GetAssetPath().GetPackageName())
#endif			
			{
				return loadedStreamLevel;
			}
		}
	}
	return nullptr;
}

void CDungeonGeneratorCore::LoadStreamLevelImplement(UWorld* world, const FSoftObjectPath& path, const FTransform& transform)
{
#if UE_VERSION_NEWER_THAN(5, 0, 0)
	const FName& longPackageName = path.GetLongPackageFName();
#else
	const FName& longPackageName = FName(path.GetLongPackageName());
#endif
	ULevelStreaming* levelStreaming;

	levelStreaming = UGameplayStatics::GetStreamingLevel(world, longPackageName);
	if (IsValid(levelStreaming))
	{
		UnloadStreamLevelImplement(world, path, true);
	}

	FLatentActionInfo LatentInfo;
	UGameplayStatics::LoadStreamLevel(world, longPackageName, false, false, LatentInfo);

	levelStreaming = UGameplayStatics::GetStreamingLevel(world, longPackageName);
	if (IsValid(levelStreaming))
	{
		levelStreaming->LevelTransform = transform;
		levelStreaming->SetShouldBeVisible(true);
	}
}

void CDungeonGeneratorCore::UnloadStreamLevelImplement(UWorld* world, const FSoftObjectPath& path, const bool shouldBlockOnUnload)
{
#if UE_VERSION_NEWER_THAN(5, 0, 0)
	const FName& longPackageName = path.GetLongPackageFName();
#else
	const FName& longPackageName = FName(path.GetLongPackageName());
#endif
	FLatentActionInfo LatentInfo;
	UGameplayStatics::UnloadStreamLevel(world, longPackageName, LatentInfo, shouldBlockOnUnload);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITOR
void CDungeonGeneratorCore::DrawDebugInformation(const bool showRoomAisleInfomation, const bool showVoxelGridType) const
{
	// 部屋と接続情報のデバッグ情報を表示します
	if (showRoomAisleInfomation)
		DrawRoomAisleInformation();

	// ボクセルグリッドのデバッグ情報を表示します
	if (showVoxelGridType)
		DrawVoxelGridType();

#if 0
	// ダンジョン全体の領域を可視化
	{
		const FBox& bounding = BoundingBox();
		UKismetSystemLibrary::DrawDebugBox(
			FindWorld(),
			bounding.GetCenter(),
			bounding.GetExtent(),
			FColor::Orange,
			FRotator::ZeroRotator,
			0.f,
			5.f
		);
	}
#endif
}

void CDungeonGeneratorCore::DrawRoomAisleInformation() const
{
	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (!IsValid(parameter))
		return;

	check(mGenerator);

	mGenerator->ForEach([this, parameter](const std::shared_ptr<const dungeon::Room>& room)
		{
			UWorld* world = mWorld.Get();
			if (IsValid(world))
			{
				UKismetSystemLibrary::DrawDebugBox(
					world,
					room->GetCenter() * parameter->GetGridSize(),
					room->GetExtent() * parameter->GetGridSize(),
					FColor::Magenta,
					FRotator::ZeroRotator,
					0.f,
					10.f
				);

				UKismetSystemLibrary::DrawDebugSphere(
					world,
					room->GetGroundCenter() * parameter->GetGridSize(),
					10.f,
					12,
					FColor::Magenta,
					0.f,
					2.f
				);
			}
		}
	);

	mGenerator->EachAisle([this, parameter](const dungeon::Aisle& edge)
		{
			UWorld* world = mWorld.Get();
			if (IsValid(world))
			{
				UKismetSystemLibrary::DrawDebugLine(
					world,
					*edge.GetPoint(0) * parameter->GetGridSize(),
					*edge.GetPoint(1) * parameter->GetGridSize(),
					FColor::Red,
					0.f,
					5.f
				);

				const FVector start(static_cast<int32>(edge.GetPoint(0)->X), static_cast<int32>(edge.GetPoint(0)->Y), static_cast<int32>(edge.GetPoint(0)->Z));
				const FVector goal(static_cast<int32>(edge.GetPoint(1)->X), static_cast<int32>(edge.GetPoint(1)->Y), static_cast<int32>(edge.GetPoint(1)->Z));
				UKismetSystemLibrary::DrawDebugSphere(
					world,
					start * parameter->GetGridSize() + FVector(parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f),
					10.f,
					12,
					FColor::Green,
					0.f,
					5.f
				);
				UKismetSystemLibrary::DrawDebugSphere(
					world,
					goal * parameter->GetGridSize() + FVector(parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f),
					10.f,
					12,
					FColor::Red,
					0.f,
					5.f
				);
			}
		}
	);
}

void CDungeonGeneratorCore::DrawVoxelGridType() const
{
	const UDungeonGenerateParameter* parameter = mParameter.Get();
	if (!IsValid(parameter))
		return;

	check(mGenerator);

	mGenerator->GetVoxel()->Each([this, parameter](const FIntVector& location, const dungeon::Grid& grid)
		{
			UWorld* world = mWorld.Get();
			if (IsValid(world))
			{
				//if (grid.GetType() == dungeon::Grid::Aisle || grid.GetType() == dungeon::Grid::Slope)
				if (grid.GetType() != dungeon::Grid::Type::Empty && grid.GetType() != dungeon::Grid::Type::OutOfBounds)
				{
					const FVector halfGrid(parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f, parameter->GetGridSize() / 2.f);
					UKismetSystemLibrary::DrawDebugBox(
						world,
						FVector(location.X, location.Y, location.Z) * parameter->GetGridSize() + halfGrid,
						halfGrid * 0.95,
						grid.GetTypeColor(),
						FRotator::ZeroRotator,
						0.f,
						5.f
					);
				}
			}

			return true;
		}
	);
}
#endif
