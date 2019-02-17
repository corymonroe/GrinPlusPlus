#include "TxHashSetValidator.h"
#include "TxHashSetImpl.h"
#include "KernelMMR.h"
#include "Common/MMR.h"
#include "Common/MMRUtil.h"
#include "Common/MMRHashUtil.h"
#include "KernelSumValidator.h"
#include "KernelSignatureValidator.h"

#include <HexUtil.h>
#include <Infrastructure/Logger.h>
#include <BlockChainServer.h>
#include <async++.h>

TxHashSetValidator::TxHashSetValidator(const IBlockChainServer& blockChainServer)
	: m_blockChainServer(blockChainServer)
{

}

// TODO: Where do we validate the data in MMR actually hashes to HashFile's hash?
std::unique_ptr<BlockSums> TxHashSetValidator::Validate(TxHashSet& txHashSet, const BlockHeader& blockHeader) const
{
	const KernelMMR& kernelMMR = *txHashSet.GetKernelMMR();
	const OutputPMMR& outputPMMR = *txHashSet.GetOutputPMMR();
	const RangeProofPMMR& rangeProofPMMR = *txHashSet.GetRangeProofPMMR();

	// Validate size of each MMR matches blockHeader
	if (!ValidateSizes(txHashSet, blockHeader))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid MMR size.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate MMR hashes in parallel
	async::task<bool> kernelTask = async::spawn([this, &kernelMMR] { return this->ValidateMMRHashes(kernelMMR); });
	async::task<bool> outputTask = async::spawn([this, &outputPMMR] { return this->ValidateMMRHashes(outputPMMR); });
	async::task<bool> rangeProofTask = async::spawn([this, &rangeProofPMMR] { return this->ValidateMMRHashes(rangeProofPMMR); });

	const bool mmrHashesValidated = async::when_all(kernelTask, outputTask, rangeProofTask).then(
		[](std::tuple<async::task<bool>, async::task<bool>, async::task<bool>> results) -> bool {
		return std::get<0>(results).get() && std::get<1>(results).get() && std::get<2>(results).get();
	}).get();
	
	if (!mmrHashesValidated)
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid MMR hashes.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate root for each MMR matches blockHeader
	if (!txHashSet.ValidateRoots(blockHeader))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid MMR roots.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate the full kernel history (kernel MMR root for every block header).
	if (!ValidateKernelHistory(*txHashSet.GetKernelMMR(), blockHeader))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid kernel history.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate kernel sums
	Commitment outputSum(CBigInteger<33>::ValueOf(0));
	Commitment kernelSum(CBigInteger<33>::ValueOf(0));
	if (!KernelSumValidator().ValidateKernelSums(txHashSet, blockHeader, outputSum, kernelSum))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid kernel sums.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate the rangeproof associated with each unspent output.
	if (!ValidateRangeProofs(txHashSet, blockHeader))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid range proof.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	// Validate kernel signatures
	if (!KernelSignatureValidator().ValidateKernelSignatures(*txHashSet.GetKernelMMR()))
	{
		LoggerAPI::LogError("TxHashSetValidator::Validate - Invalid kernel signatures.");
		return std::unique_ptr<BlockSums>(nullptr);
	}

	return std::make_unique<BlockSums>(BlockSums(std::move(outputSum), std::move(kernelSum)));
}

bool TxHashSetValidator::ValidateSizes(TxHashSet& txHashSet, const BlockHeader& blockHeader) const
{
	if (txHashSet.GetKernelMMR()->GetSize() != blockHeader.GetKernelMMRSize())
	{
		LoggerAPI::LogError("TxHashSetValidator::ValidateSizes - Kernel size not matching for header " + HexUtil::ConvertHash(blockHeader.GetHash()));
		return false;
	}

	if (txHashSet.GetOutputPMMR()->GetSize() != blockHeader.GetOutputMMRSize())
	{
		LoggerAPI::LogError("TxHashSetValidator::ValidateSizes - Output size not matching for header " + HexUtil::ConvertHash(blockHeader.GetHash()));
		return false;
	}

	if (txHashSet.GetRangeProofPMMR()->GetSize() != blockHeader.GetOutputMMRSize())
	{
		LoggerAPI::LogError("TxHashSetValidator::ValidateSizes - RangeProof size not matching for header " + HexUtil::ConvertHash(blockHeader.GetHash()));
		return false;
	}

	return true;
}

// TODO: This probably belongs in MMRHashUtil.
bool TxHashSetValidator::ValidateMMRHashes(const MMR& mmr) const
{
	const uint64_t size = mmr.GetSize();
	for (uint64_t i = 0; i < size; i++)
	{
		const uint64_t height = MMRUtil::GetHeight(i);
		if (height > 0)
		{
			const std::unique_ptr<Hash> pParentHash = mmr.GetHashAt(i);
			if (pParentHash != nullptr)
			{
				const uint64_t leftIndex = MMRUtil::GetLeftChildIndex(i, height);
				const std::unique_ptr<Hash> pLeftHash = mmr.GetHashAt(leftIndex);

				const uint64_t rightIndex = MMRUtil::GetRightChildIndex(i);
				const std::unique_ptr<Hash> pRightHash = mmr.GetHashAt(rightIndex);

				if (pLeftHash != nullptr && pRightHash != nullptr)
				{
					const Hash expectedHash = MMRHashUtil::HashParentWithIndex(*pLeftHash, *pRightHash, i);
					if (*pParentHash != expectedHash)
					{
						LoggerAPI::LogError("TxHashSetValidator::ValidateMMRHashes - Invalid parent hash at index " + std::to_string(i));
						return false;
					}
				}
			}
		}
	}

	return true;
}
bool TxHashSetValidator::ValidateKernelHistory(const KernelMMR& kernelMMR, const BlockHeader& blockHeader) const
{
	for (uint64_t height = 0; height <= blockHeader.GetHeight(); height++)
	{
		std::unique_ptr<BlockHeader> pHeader = m_blockChainServer.GetBlockHeaderByHeight(height, EChainType::CANDIDATE);
		if (pHeader == nullptr)
		{
			LoggerAPI::LogError("TxHashSetValidator::ValidateKernelHistory - No header found at height " + std::to_string(height));
			return false;
		}
		
		if (kernelMMR.Root(pHeader->GetKernelMMRSize()) != pHeader->GetKernelRoot())
		{
			LoggerAPI::LogError("TxHashSetValidator::ValidateKernelHistory - Kernel root not matching for header at height " + std::to_string(height));
			return false;
		}
	}

	return true;
}

bool TxHashSetValidator::ValidateRangeProofs(TxHashSet& txHashSet, const BlockHeader& blockHeader) const
{
	std::vector<Commitment> commitments;
	std::vector<RangeProof> rangeProofs;

	for (uint64_t mmrIndex = 0; mmrIndex < txHashSet.GetOutputPMMR()->GetSize(); mmrIndex++)
	{
		std::unique_ptr<OutputIdentifier> pOutput = txHashSet.GetOutputPMMR()->GetOutputAt(mmrIndex);
		if (pOutput != nullptr)
		{
			std::unique_ptr<RangeProof> pRangeProof = txHashSet.GetRangeProofPMMR()->GetRangeProofAt(mmrIndex);
			if (pRangeProof == nullptr)
			{
				LoggerAPI::LogError("TxHashSetValidator::ValidateRangeProofs - No rangeproof found at mmr index " + std::to_string(mmrIndex));
				return false;
			}

			commitments.push_back(pOutput->GetCommitment());
			rangeProofs.push_back(*pRangeProof);

			if (commitments.size() >= 2000)
			{
				if (!Crypto::VerifyRangeProofs(commitments, rangeProofs))
				{
					LoggerAPI::LogError("TxHashSetValidator::ValidateRangeProofs - Failed to verify rangeproofs.");
					return false;
				}

				commitments.clear();
				rangeProofs.clear();
			}
		}
	}

	if (!commitments.empty())
	{
		if (!Crypto::VerifyRangeProofs(commitments, rangeProofs))
		{
			LoggerAPI::LogError("TxHashSetValidator::ValidateRangeProofs - Failed to verify rangeproofs.");
			return false;
		}
	}
	
	return true;
}