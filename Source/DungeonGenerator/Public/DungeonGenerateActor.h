/**
\author		Shun Moriya
\copyright	2023- Shun Moriya
All Rights Reserved.
*/

#pragma once
#include "DungeonRoomParts.h"
#include <GameFramework/Actor.h>
#include <memory>
#include "DungeonGenerateActor.generated.h"

class CDungeonGeneratorCore;
class UDungeonGenerateParameter;
class UDungeonMiniMapTextureLayer;
class UDungeonTransactionalHierarchicalInstancedStaticMeshComponent;

namespace dungeon
{
	class Room;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDungeonGeneratorActorSignature, const FTransform&, transform);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDungeonGeneratorDoorSignature, AActor*, doorActor, EDungeonRoomProps, props);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDungeonGeneratorPlayerStartSignature, const FVector&, location);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDungeonGeneratorDelegete, bool, StreamingLevel, EDungeonRoomParts, DungeonRoomParts, const FBox&, RoomRect);

/**
Dungeon generation actor class
*/
UCLASS(Blueprintable, BlueprintType)
class DUNGEONGENERATOR_API ADungeonGenerateActor : public AActor
{
	GENERATED_BODY()

public:
	/**
	constructor
	*/
	explicit ADungeonGenerateActor(const FObjectInitializer& initializer);

	/**
	destructor
	*/
	virtual ~ADungeonGenerateActor();

	/**
	Generate new dungeon
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		void GenerateDungeon();

	/**
	Destroy  dungeon
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		void DestroyDungeon();

	/**
	Finds the floor from the world Z coordinate
	\param[in]	z	Z coordinate of world
	\return		floor
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		int32 FindFloorHeight(const float z) const;

	/**
	Finds the Z coordinate of the grid from the world Z coordinate
	\param[in]	z	Z coordinate of world
	\return		Z coordinate of the grid
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		int32 FindVoxelHeight(const float z) const;

	/**
	Generates a texture layer for the minimap
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		UDungeonMiniMapTextureLayer* GenerateMiniMapTextureLayerWithSize(const int32 textureWidth = 512);

	/**
	Generates a texture layer for the minimap
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		UDungeonMiniMapTextureLayer* GenerateMiniMapTextureLayerWithScale(const int32 dotScale = 1);

	/**
	Get a texture layer for a generated minimap
	*/
	UFUNCTION(BlueprintCallable, Category = "DungeonGenerator")
		UDungeonMiniMapTextureLayer* GetGeneratedMiniMapTextureLayer() const;

	// AActor overrides
	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

private:
	static void BeginAddInstance(TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*>& meshs);

	static void AddInstance(TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*>& meshs, const UStaticMesh* staticMesh, const FTransform& transform);

	static void EndAddInstance(TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*>& meshs);

	static inline FBox ToWorldBoundingBox(const std::shared_ptr<const dungeon::Room>& room, const float gridSize);

	void PreGenerateImplementation();
	void PostGenerateImplementation();
	void DestroyImplementation();
	void MovePlayerStart();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DungeonGenerator")
		UDungeonGenerateParameter* DungeonGenerateParameter = nullptr;

#if WITH_EDITORONLY_DATA
	//! Random number seeds
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator")
		int32 GeneratedRandomSeed = 0;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DungeonGenerator")
		bool InstancedStaticMesh = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> FloorMeshs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> SlopeMeshs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> WallMeshs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> RoomRoofMeshs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> AisleRoofMeshs;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DungeonGenerator")
		TArray<UDungeonTransactionalHierarchicalInstancedStaticMeshComponent*> PillarMeshs;

	// event
	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreateFloor;

	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreateSlope;

	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreateWall;

	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreateRoomRoof;

	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreateAisleRoof;

	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorActorSignature OnCreatePillar;


	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorDoorSignature OnResetDoor;





	/*
	Notification to move the player to the starting position
	Called at the timing of PreInitializeComponents
	*/
	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorPlayerStartSignature OnMovePlayerStart;

	/*
	部屋の生成通知
	通知先で敵アクターなどを生成する事を想定しています
	最初のTickで呼び出されます
	（検討中の機能）
	*/
	UPROPERTY(BlueprintAssignable, Category = "Event")
		FDungeonGeneratorDelegete OnRoomCreated;

	// Cache of the UIDungeonMiniMapTextureLayer
	UPROPERTY(BlueprintReadOnly, Transient, Category = "DungeonGenerator")
		UDungeonMiniMapTextureLayer* DungeonMiniMapTextureLayer;
	// TObjectPtr<UDungeonMiniMapTextureLayer> not used for UE4 compatibility


	// build job tag
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Detail")
		FString BuildJobTag;

	// license tag
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Detail")
		FString LicenseTag;

	// License ID
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Detail")
		FString LicenseId;

#if WITH_EDITORONLY_DATA && (UE_BUILD_SHIPPING == 0)
	// Displays debugging information on room and connection information
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Debug")
		bool ShowRoomAisleInformation = false;

	// Displays voxel grid debugging information
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Debug")
		bool ShowVoxelGridType = false;

	// Displays debugging information for the voxel grid at the player's position
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "DungeonGenerator|Debug")
		bool ShowVoxelGridTypeAtPlayerLocation = false;

private:
	void DrawDebugInformation();
#endif

private:
	// Cache of the UIDungeonMiniMapTextureLayer
	std::shared_ptr<CDungeonGeneratorCore> mDungeonGeneratorCore;

	bool mPostGenerated = false;
};
