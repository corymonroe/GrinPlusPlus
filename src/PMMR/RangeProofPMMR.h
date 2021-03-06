#pragma once

#include "Common/PruneableMMR.h"

#include <Crypto/RangeProof.h>
#include <filesystem.h>

#define RANGE_PROOF_SIZE 683

class RangeProofPMMR : public PruneableMMR<RANGE_PROOF_SIZE, RangeProof>
{
public:
	static std::shared_ptr<RangeProofPMMR> Load(const fs::path& txHashSetPath)
	{
		std::shared_ptr<HashFile> pHashFile = HashFile::Load(txHashSetPath.u8string() + "rangeproof/pmmr_hash.bin");

		if (!FileUtil::Exists(txHashSetPath.u8string() + "rangeproof/pmmr_leafset.bin") && FileUtil::Exists(txHashSetPath.u8string() + "rangeproof/pmmr_leaf.bin"))
		{
			Roaring outputBitmap;
			std::vector<unsigned char> outputBytes;
			if (FileUtil::ReadFile(txHashSetPath.u8string() + "rangeproof/pmmr_leaf.bin", outputBytes))
			{
				outputBitmap = Roaring::readSafe((const char*)outputBytes.data(), outputBytes.size());
			}
			BitmapFile::Create(txHashSetPath.u8string() + "rangeproof/pmmr_leafset.bin", outputBitmap);
		}

		std::shared_ptr<LeafSet> pLeafSet = LeafSet::Load(txHashSetPath.u8string() + "rangeproof/pmmr_leafset.bin");
		std::shared_ptr<PruneList> pPruneList = PruneList::Load(txHashSetPath.u8string() + "rangeproof/pmmr_prun.bin");
		std::shared_ptr<DataFile<RANGE_PROOF_SIZE>> pDataFile = DataFile<RANGE_PROOF_SIZE>::Load(txHashSetPath.u8string() + "rangeproof/pmmr_data.bin");

		return std::make_shared<RangeProofPMMR>(RangeProofPMMR(pHashFile, pLeafSet, pPruneList, pDataFile));
	}

	virtual ~RangeProofPMMR() = default;

private:
	RangeProofPMMR(
		std::shared_ptr<HashFile> pHashFile,
		std::shared_ptr<LeafSet> pLeafSet,
		std::shared_ptr<PruneList> pPruneList,
		std::shared_ptr<DataFile<RANGE_PROOF_SIZE>> pDataFile)
		: PruneableMMR<RANGE_PROOF_SIZE, RangeProof>(pHashFile, pLeafSet, pPruneList, pDataFile)
	{

	}
};