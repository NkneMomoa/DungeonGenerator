/**
ボクセルなどに利用するグリッド情報のソースファイル

\author		Shun Moriya
\copyright	2023- Shun Moriya
All Rights Reserved.
*/

#include "Grid.h"
#include <cassert>

namespace dungeon
{
	bool Grid::IsKindOfRoomType() const noexcept
	{
		return
			mType == Type::Floor ||
			mType == Type::Deck ||
			mType == Type::Gate;
	}

	bool Grid::IsKindOfRoomTypeWithoutGate() const noexcept
	{
		return
			mType == Type::Floor ||
			mType == Type::Deck;
	}

	bool Grid::IsKindOfGateType() const noexcept
	{
		return
			mType == Type::Gate;
	}

	bool Grid::IsKindOfAisleType() const noexcept
	{
		return
			mType == Type::Aisle;
	}

	bool Grid::IsKindOfSlopeType() const noexcept
	{
		return
			mType == Type::Slope ||
			mType == Type::Atrium;
	}

	bool Grid::IsKindOfSpatialType() const noexcept
	{
		return
			mType == Type::Empty ||
			mType == Type::OutOfBounds;
	}
	
	bool Grid::IsHorizontallyPassable() const noexcept
	{
		return
			mType == Type::Floor ||
			mType == Type::Deck ||
			mType == Type::Gate ||
			mType == Type::Aisle ||
			mType == Type::Slope ||
			mType == Type::Atrium;
	}

	bool Grid::IsHorizontallyNotPassable() const noexcept
	{
		return IsHorizontallyPassable() == false;
	}

	bool Grid::IsVerticallyPassable() const noexcept
	{
		return
			mType == Type::Floor ||
			mType == Type::Deck ||
			mType == Type::Gate ||
			//mType == Type::Aisle ||
			mType == Type::Slope ||
			mType == Type::Atrium;
	}

	bool Grid::IsVerticallyNotPassable() const noexcept
	{
		return IsVerticallyPassable() == false;
	}

	/*
	自身からtoGridを見た時に床が生成されるか判定します
	*/
	bool Grid::CanBuildFloor(const Grid& toGrid, const bool checkNoMeshGeneration) const noexcept
	{
		if (checkNoMeshGeneration && IsNoFloorMeshGeneration())
			return false;

		if (IsKindOfRoomType())
		{
			return
				toGrid.GetIdentifier() != GetIdentifier() ||
				toGrid.IsKindOfAisleType() ||
				toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfAisleType())
		{
			return
				toGrid.GetIdentifier() != GetIdentifier() ||
				toGrid.IsKindOfRoomType() ||
				toGrid.IsKindOfAisleType() ||
				toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}

		return false;
	}

	/*
	斜面が生成されるか判定します
	*/
	bool Grid::CanBuildSlope() const noexcept
	{
		return mType == Type::Slope;
	}

	/*
	自身からtoGridを見た時に屋根が生成されるか判定します
	*/
	bool Grid::CanBuildRoof(const Grid& toGrid, const bool checkNoMeshGeneration) const noexcept
	{
		if (checkNoMeshGeneration && IsNoRoofMeshGeneration())
			return false;

		if (IsKindOfRoomType())
		{
			return
				(toGrid.mType == Type::Deck || toGrid.mType == Type::Gate) ||
				toGrid.IsKindOfAisleType() ||
				toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}
		else if(IsKindOfAisleType())
		{
			return
				toGrid.IsKindOfRoomType() ||
				toGrid.IsKindOfAisleType() ||
				toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfSlopeType())
		{
			return
				toGrid.IsKindOfRoomType() ||
				toGrid.IsKindOfAisleType() ||
				toGrid.mType == Type::Slope ||
				toGrid.IsKindOfSpatialType();
		}

		return false;
	}

	/*
	自身からtoGridを見た時に壁が生成されるか判定します
	*/
	bool Grid::CanBuildWall(const Grid& toGrid, const Direction::Index direction, const bool mergeRooms) const noexcept
	{
		// 部屋と部屋の間に壁を生成する？
		if (!mergeRooms)
		{
			/*
			部屋と部屋が隣接している場合、
			グリッドの識別番号（＝部屋の識別番号）が不一致なら壁がある
			*/
			if (IsKindOfRoomTypeWithoutGate() && toGrid.IsKindOfRoomTypeWithoutGate())
			{
				return mIdentifier != toGrid.mIdentifier;
			}
		}

		if (IsKindOfGateType())
		{
			if (toGrid.IsKindOfRoomType() || toGrid.IsKindOfSlopeType())
			{
				// 識別子が異なっていて、かつ方向が交差していたら壁
				return
					GetIdentifier() != toGrid.GetIdentifier() &&
					GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction);
			}

			// 空間なら壁
			return toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfRoomTypeWithoutGate())
		{
			// 門は部屋系のグリッドでもあるので注意
			return
				toGrid.IsKindOfAisleType() ||
				toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfAisleType())
		{
			if (toGrid.IsKindOfAisleType() || toGrid.IsKindOfSlopeType())
			{
				// 通路の識別子が違うなら壁
				return toGrid.GetIdentifier() != GetIdentifier();
			}

			return
				toGrid.IsKindOfRoomTypeWithoutGate() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (mType == Type::Slope)
		{
			if (toGrid.IsKindOfSlopeType())
			{
				// 方向が交差していたら壁
				// グリッドの識別番号が不一致なら壁がある
				return
					(toGrid.GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction)) ||
					(toGrid.mIdentifier != mIdentifier);
			}

			return toGrid.IsKindOfSpatialType();
		}
		else if (mType == Type::Atrium)
		{
			if (toGrid.IsKindOfSlopeType())
			{
				// 方向が交差していたら壁
				// グリッドの識別番号が不一致なら壁がある
				return toGrid.GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction) ||
					(toGrid.mIdentifier != mIdentifier);
			}

			return toGrid.IsKindOfSpatialType();
		}

		return false;
	}

	/*
	自身からtoGridを見た時に壁が生成されるか判定します
	*/
	bool Grid::CanBuildWallForMinimap(const Grid& toGrid, const Direction::Index direction, const bool mergeRooms) const noexcept
	{
		// 部屋と部屋の間に壁を生成する？
		if (!mergeRooms)
		{
			/*
			部屋と部屋が隣接している場合、
			グリッドの識別番号（＝部屋の識別番号）が不一致なら壁がある
			*/
			if (IsKindOfRoomTypeWithoutGate() && toGrid.IsKindOfRoomTypeWithoutGate())
			{
				return mIdentifier != toGrid.mIdentifier;
			}
		}

		if (IsKindOfGateType())
		{
			if (toGrid.IsKindOfRoomType() || toGrid.IsKindOfSlopeType())
			{
				// 識別子が異なっていて、かつ方向が交差していたら壁
				return
					GetIdentifier() != toGrid.GetIdentifier() &&
					GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction);
			}

			// 空間なら壁
			return toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfRoomTypeWithoutGate())
		{
			// 門は部屋系のグリッドでもあるので注意
			return
				toGrid.IsKindOfAisleType() ||
				//toGrid.IsKindOfSlopeType() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (IsKindOfAisleType())
		{
			if (toGrid.IsKindOfAisleType() || toGrid.IsKindOfSlopeType())
			{
				// 通路の識別子が違うなら壁
				return toGrid.GetIdentifier() != GetIdentifier();
			}

			return
				toGrid.IsKindOfRoomTypeWithoutGate() ||
				toGrid.IsKindOfSpatialType();
		}
		else if (mType == Type::Slope)
		{
			if (toGrid.IsKindOfSlopeType())
			{
				// 方向が交差していたら壁
				// グリッドの識別番号が不一致なら壁がある
				return
					(toGrid.GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction)) ||
					(toGrid.mIdentifier != mIdentifier);
			}

			return toGrid.IsKindOfSpatialType();
		}
		else if (mType == Type::Atrium)
		{
			if (toGrid.IsKindOfSlopeType())
			{
				// 方向が交差していたら壁
				// グリッドの識別番号が不一致なら壁がある
				return toGrid.GetDirection().IsNorthSouth() != Direction::IsNorthSouth(direction) ||
					(toGrid.mIdentifier != mIdentifier);
			}

			return toGrid.IsKindOfSpatialType();
		}

		return false;
	}

	/*
	自身からtoGridを見た時に柱が生成されるか判定します
	*/
	bool Grid::CanBuildPillar(const Grid& toGrid) const noexcept
	{
		/*
		判定が怪しいので見直してください
		*/
		return
			toGrid.IsHorizontallyPassable() &&
			(
				toGrid.GetType() != dungeon::Grid::Type::Empty &&
				/*result.GetType() != dungeon::Grid::Type::Gate &&*/
				toGrid.GetType() != dungeon::Grid::Type::Atrium &&
				toGrid.GetType() != dungeon::Grid::Type::Slope
			);
	}

	/*
	自身からtoGridを見た時に扉が生成されるか判定します
	*/
	bool Grid::CanBuildGate(const Grid& toGrid, const Direction::Index direction) const noexcept
	{
		if (mType == Type::Gate)
		{
			/*
			門と門の間に通路が無い場合は、
			ゴールと反対方向のグリッドのみ門を生成する
			*/
			if (toGrid.mType == Type::Gate)
			{
				return
					mDirection == toGrid.mDirection &&
					mDirection.Inverse() == Direction(direction);
			}
			/*
			階段の正面が門と同じ方向なら門を生成する
			*/
			else if (toGrid.IsKindOfSlopeType())
			{
				return
					mDirection.IsNorthSouth() == toGrid.GetDirection().IsNorthSouth() &&
					mDirection.IsNorthSouth() == Direction(direction).IsNorthSouth();
			}

			return toGrid.IsKindOfAisleType();
		}

		return false;
	}

	const FColor& Grid::GetTypeColor() const noexcept
	{
		static const FColor colors[] = {
			FColor::Blue,		// Floor
			FColor::Yellow,		// Deck
			FColor::Red,		// Gate
			FColor::Green,		// Aisle
			FColor::Magenta,	// Slope
			FColor::Cyan,		// Atrium
			FColor::Black,		// Empty
			FColor::Black,		// OutOfBounds
		};
		static constexpr size_t ColorSize = sizeof(colors) / sizeof(colors[0]);
		static_assert(ColorSize == static_cast<size_t>(TypeSize));
		const size_t index = static_cast<size_t>(mType);
		return colors[index];
	}

	const FString& Grid::GetTypeName() const noexcept
	{
		static const FString names[] = {
			"Floor",
			"Deck",
			"Gate",
			"Aisle",
			"Slope",
			"Atrium",
			"Empty",
			"OutOfBounds"
		};
		static constexpr size_t NameSize = sizeof(names) / sizeof(names[0]);
		static_assert(NameSize == static_cast<size_t>(TypeSize));
		const size_t index = static_cast<size_t>(mType);
		return names[index];
	}

	const FString& Grid::GetPropsName() const noexcept
	{
		static const FString names[] = {
			"None",
			"Lock",
			"UniqueLock"
		};
		static constexpr size_t NameSize = sizeof(names) / sizeof(names[0]);
		static_assert(NameSize == static_cast<size_t>(PropsSize));
		const size_t index = static_cast<size_t>(mProps);
		return names[index];
	}
}
