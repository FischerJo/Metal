#ifndef READQUEUE_H
#define READQUEUE_H

#include <string>
#include <fstream>
#include <unordered_map>
#include <array>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "CONST.h"
#include "RefGenome.h"
#include "Read.h"

class ReadQueue
{

    public:

        ReadQueue(const char* filePath, RefGenome& ref);


        // Parses a chunk of the ifstream file
        // Reads up to MyConst::CHUNKSIZE many reads and saves them
        // ARGUMENT:
        //          procReads   will contain number of reads that have been read into buffer
        // returns true if neither read error nor EOF occured, false otherwise
        bool parseChunk(unsigned int& procReads);

        // Match all reads in readBuffer to reference genome
        // ARGUMENT:
        //          procReads   number of reads to match
        // First retrieve seeds using getSeeds(...)
        // Filter seeds using filterHeuSeeds(...) according to simple heuristic
        // Extend remaining seeds with BitMatch(...)
        bool matchReads(const unsigned int& procReads);


    private:

        // filters seeds according to simple counting criteria
        // #kmers of one metaCpG should be > READLEN - KMERLEN + 1 - (KMERLEN * MISCOUNT)
        inline void filterHeuSeeds(std::vector<std::vector<KMER::kmer> >& seedsK, std::vector<std::vector<bool> >& seedsS, const unsigned int readSize)
        {

            std::vector<unsigned int>& threadCount = counts[omp_get_thread_num()];
            // fill with zeroes
            threadCount.assign(ref.metaCpGs.size(), 0);

            // count occurences of meta CpGs
            for (unsigned int i = 0; i < seedsK.size(); ++i)
            {

                // last visited id in this table entry
                // avoid counting metaCpGs more then once per kmer
                // note that metaCpGs are hashed in reverse order
                uint64_t lastId = 0xffffffffffffffffULL;
                for (unsigned int j = 0; j < seedsK[i].size(); ++j)
                {

                    uint64_t metaId = KMER::getMetaCpG(seedsK[i][j]);
                    // check if we visited meta CpG before
                    if (metaId == lastId)
                    {
                        continue;
                    }

                    lastId = metaId;
                    ++threadCount[metaId];
                }
            }

            // More than cutoff many kmers are required per metaCpG
            const unsigned int countCut = readSize - MyConst::KMERLEN + 1 - (MyConst::KMERLEN * MyConst::MISCOUNT);

            // TODO: dummy values instead of reallocations

            // throw out rare metaCpGs
            for (unsigned int i = 0; i < seedsK.size(); ++i)
            {

                std::vector<KMER::kmer> filteredSeedsK;
                std::vector<bool> filteredSeedsS;
                filteredSeedsK.reserve(seedsK[i].size());
                filteredSeedsS.reserve(seedsK[i].size());
                for (unsigned int j = 0; j < seedsK[i].size(); ++j)
                {

                    // test for strict heuristic criterias
                    if (threadCount[KMER::getMetaCpG(seedsK[i][j])] >= countCut)
                    {

                        filteredSeedsK.push_back(seedsK[i][j]);
                        filteredSeedsS.push_back(seedsS[i][j]);

                    }
                }
                filteredSeedsK.shrink_to_fit();
                filteredSeedsS.shrink_to_fit();
                seedsK[i] = std::move(filteredSeedsK);
                seedsS[i] = std::move(filteredSeedsS);
            }

        }

        // Do a bitmatching between the specified seeds of the reference and the read r or the reverse complement (Rev suffix)
        //
        // ARGUMENTS:
        //              r       read to match with
        //              seedsK  list of kmer positions in reference that should be checked
        //              seedsS  list of flags for each kmer in seedsK stating if it is from forward or reverse reference strand
        //
        // RETURN:
        //              void
        //
        // MODIFICATIONS:
        //              function will filter seedsK and seedsS to contain only reference kmers that match read kmer under bitmask
        //              comparison
        inline void bitMatching(const Read& r, std::vector<std::vector<KMER::kmer> >& seedsK, std::vector<std::vector<bool> >& seedsS)
        {

            // masking for actual kmer bits
            constexpr uint64_t signiBits = 0xffffffffffffffffULL >> (64 - (2*MyConst::KMERLEN));
            // bit representation of current kmer of read
            uint64_t kmerBits = 0;
            // generate bit representation of first kmerlen - 1 letters of read
            for (unsigned int i = 0; i < (MyConst::KMERLEN - 1); ++i)
            {

                kmerBits = kmerBits << 2;
                kmerBits |= BitFun::getBitRepr(r.seq[i]);

            }

            // Note that seedsK must have the same size as seedsS anyway, this way we may have a cache hit
            std::vector<std::vector<KMER::kmer> > newSeedsK(seedsK.size());
            std::vector<std::vector<bool> > newSeedsS(seedsK.size());

            // go over each read kmer and compare with reference seeds
            for (unsigned int offset = 0; offset < (r.seq.size() - MyConst::KMERLEN + 1); ++offset)
            {

                // retrieve seeds for current kmer
                std::vector<KMER::kmer>& localSeedsK = seedsK[offset];
                std::vector<bool>& localSeedsS = seedsS[offset];
                // reserve some space for result seedlist
                newSeedsK[offset].reserve(localSeedsK.size());
                newSeedsS[offset].reserve(localSeedsK.size());

                // update current read kmer representation
                kmerBits = kmerBits << 2;
                kmerBits = (kmerBits | BitFun::getBitRepr(r.seq[offset + MyConst::KMERLEN - 1])) & signiBits;

                // iterate over corresponding seeds for reference
                for (unsigned int i = 0; i < localSeedsK.size(); ++i)
                {

                    KMER::kmer& refKmer = localSeedsK[i];

                    // will hold the first CpG in metaCpG after retrieving the meta CpG info
                    uint8_t chrom;
                    // will hold the position of the meta CpG in the genome (chromosomal region specified by chrom of the genome)
                    uint32_t pos = 0;
                    // retrieve reference bit representation
                    //
                    // First retrieve meta CpG info
                    if (KMER::isStartCpG(refKmer))
                    {

                        const uint32_t& cpgInd = ref.metaStartCpGs[KMER::getMetaCpG(refKmer)].start;
                        chrom = ref.cpgStartTable[cpgInd].chrom;

                    } else {

                        const uint32_t& cpgInd = ref.metaCpGs[KMER::getMetaCpG(refKmer)].start;
                        chrom = ref.cpgTable[cpgInd].chrom;
                        pos = ref.cpgTable[cpgInd].pos;

                    }
                    // will hold bit representation of seed
                    uint64_t refKmerBit;
                    // decide if forward or reverse strand of reference
                    //
                    // if forward
                    if (localSeedsS[i])
                    {
                        // retrieve sequence in forward strand
                        refKmerBit = ref.genomeBit[chrom].getSeqKmer(pos + KMER::getOffset(refKmer));

                    // is reverse
                    } else {

                        // retrieve sequence in forward strand
                        refKmerBit = ref.genomeBit[chrom].getSeqKmerRev(pos + KMER::getOffset(refKmer));
                    }

                    // COMPARE read kmer and seed
                    //  matching is 0 iff full match
                    if ( !( refKmerBit ^ (kmerBits & BitFun::getMask(refKmerBit)) ) )
                    {

                        // if we have a match, keep this kmer and strand flag in list
                        newSeedsK[offset].emplace_back(refKmer);
                        newSeedsS[offset].emplace_back(localSeedsS[i]);
                    }
                }
                newSeedsK[offset].shrink_to_fit();
                newSeedsS[offset].shrink_to_fit();

            }

            seedsK = std::move(newSeedsK);
            seedsS = std::move(newSeedsS);

        }
        inline void bitMatchingRev(const Read& r, std::vector<std::vector<KMER::kmer> >& seedsK, std::vector<std::vector<bool> >& seedsS)
        {

            const unsigned int readSize = r.seq.size();
            // masking for actual kmer bits
            constexpr uint64_t signiBits = 0xffffffffffffffffULL >> (64 - (2*MyConst::KMERLEN));
            // bit representation of current kmer of read
            uint64_t kmerBits = 0;
            // generate bit representation of first kmerlen - 1 letters of reverse complement of read
            // Not that we start reading from right
            for (unsigned int i = readSize - 1; i > readSize - MyConst::KMERLEN; --i)
            {

                kmerBits = kmerBits << 2;
                kmerBits |= BitFun::getBitReprRev(r.seq[i]);

            }

            // Note that seedsK must have the same size as seedsS anyway, this way we may have a cache hit
            std::vector<std::vector<KMER::kmer> > newSeedsK(seedsK.size());
            std::vector<std::vector<bool> > newSeedsS(seedsK.size());

            // index for seed vector for current read kmer
            unsigned int kmerInd = 0;
            // go over each read kmer and compare with reference seeds
            for (unsigned int offset = readSize - MyConst::KMERLEN; offset > 0; --offset, ++kmerInd)
            {

                std::vector<KMER::kmer>& localSeedsK = seedsK[kmerInd];
                std::vector<bool>& localSeedsS = seedsS[kmerInd];
                // reserve some space for seedlist
                newSeedsK[kmerInd].reserve(localSeedsK.size());
                newSeedsS[kmerInd].reserve(localSeedsK.size());

                // update current read kmer representation
                kmerBits = kmerBits << 2;
                kmerBits = (kmerBits | BitFun::getBitReprRev(r.seq[offset - 1])) & signiBits;

                // iterate over corresponding seeds for reference
                for (unsigned int i = 0; i < localSeedsK.size(); ++i)
                {

                    KMER::kmer& refKmer = localSeedsK[i];

                    // will hold the chromosome index in metaCpG after retrieving the meta CpG info
                    uint8_t chrom;
                    // will hold the position of the meta CpG in the genome (chromosomal region specified by chrom of the genome)
                    uint32_t pos = 0;
                    // retrieve reference bit representation
                    //
                    // First retrieve meta CpG info
                    if (KMER::isStartCpG(refKmer))
                    {

                        uint32_t& cpgInd = ref.metaStartCpGs[KMER::getMetaCpG(refKmer)].start;
                        chrom = ref.cpgStartTable[cpgInd].chrom;

                    } else {

                        uint32_t& cpgInd = ref.metaCpGs[KMER::getMetaCpG(refKmer)].start;
                        chrom = ref.cpgTable[cpgInd].chrom;
                        pos = ref.cpgTable[cpgInd].pos;

                    }
                    // will hold bit representation of seed
                    uint64_t refKmerBit;
                    // decide if forward or reverse strand of reference
                    //
                    // if forward
                    if (localSeedsS[i])
                    {
                        // retrieve sequence in forward strand
                        refKmerBit = ref.genomeBit[chrom].getSeqKmer(pos + KMER::getOffset(refKmer));

                    // is reverse
                    } else {

                        // retrieve sequence in forward strand
                        refKmerBit = ref.genomeBit[chrom].getSeqKmerRev(pos + KMER::getOffset(refKmer));
                    }

                    // COMPARE read kmer and seed
                    //  matching is 0 iff full match
                    if ( !( refKmerBit ^ (kmerBits & BitFun::getMask(refKmerBit)) ) )
                    {

                        // if we have a match, keep this kmer and strand flag in list
                        newSeedsK[kmerInd].emplace_back(refKmer);
                        newSeedsS[kmerInd].emplace_back(localSeedsS[i]);
                    }
                }
                newSeedsK[kmerInd].shrink_to_fit();
                newSeedsS[kmerInd].shrink_to_fit();

            }

            seedsK = std::move(newSeedsK);
            seedsS = std::move(newSeedsS);
        }

        // print statistics over seed set to statFile and countFile
        //
        // statFile contains statistics over how many times (at most n) the same meta CpG appears in the seed list of one kmer
        // blocks of 4 lines show the change before countfilter, after countfilter, after bitmatch, after second countfilter
        //
        // OUTPUT FORMAT (tsv):
        // #occurences of same meta cpg   1   2   3   ...    n
        // read 1 firstLayer
        // read 1 secondLayer
        // read 1 thirdlLayer
        // read 1 fourthLayer
        // read 2 firstLayer
        // read 2 ...
        //
        //
        // countFile contains counts on how many seeds were found for the given read, 4 columns forming the layers
        // before countfilter, after countfilter, after bitmatch, after second countfilter
        //
        // OUTPUT FORMAT (tsv):
        // Layer    1   2   3   4
        // read1
        // read2
        // read3
        // ...
        //
        // function called for each layer
        //
        // ARGUMENTS:
        //              SeedsK      (current) seed set
        //
        // RETURN:
        //              void
        //
        // MODIFICATIONS:
        //              none
        //
        static constexpr unsigned int n = 400;
        std::ofstream statFile;
        std::ofstream countFile;
        void printStatistics(const std::vector<std::vector<KMER::kmer> > SeedsK);

        // input stream of file given as path to Ctor
        std::ifstream file;

        // representation of the reference genome
        RefGenome& ref;

        // buffer holding MyConst::CHUNKSIZE many reads
        std::vector<Read> readBuffer;


        // holds counts for each thread for counting heuristic
        std::array<std::vector<unsigned int>, CORENUM> counts;
};

#endif /* READQUEUE_H */