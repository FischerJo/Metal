//	Metal - A fast methylation alignment and calling tool for WGBS data.
//	Copyright (C) 2017  Jonas Fischer
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//	Jonas Fischer	jonaspost@web.de

#ifndef REFGENOME_H
#define REFGENOME_H

#include <fstream>
#include <istream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Project includes
#include "CONST.h"
#include "structs.h"
#include "DnaBitStr.h"
// #include "ntHash-1.0.2/nthash.hpp"
// spaced
#include "spaced_nthash/nthash.hpp"


// Class representing the reference genome
class RefGenome
{

    public:

        RefGenome() = delete;

        // Ctor
        // ARGUMENTS: ( see class members for full information )
        //      cpgTab      table of all CpGs in reference genome except for the ones near the start of a sequence (i.e. less then READLEN away from start)
        //      cpgStartTab table of CpGs near the start
        //      genSeq      genomic sequence seperated by chromosome
        //      noloss      flag that is true iff index must be lossless
		//      chrMap		map of internal chromosome ID to external string identifier of fasta file
        RefGenome(std::vector<struct CpG>&& cpgTab, std::vector<struct CpG>&& cpgStartTab, std::vector<std::vector<char> >& genSeq, const bool noloss, std::unordered_map<uint8_t, std::string>& chromMap);
        // ARGUMENTS:
        //      filepath    file where index was saved before using RefGenome::save(...)
        RefGenome(std::string filePath);

        ~RefGenome() = default;

        // functions to save and load the index structure represented by this class to a binary file
        void save(const std::string& filepath);
        void load(const std::string& filepath);
        inline void write_strands(std::ofstream& of);
        inline void read_strands(std::ifstream& ifs);
        inline void write_filteredKmers(std::ofstream& of);
        inline void read_filteredKmers(std::ifstream& ifs);


    // private:

        // produces all struct Meta CpGs
        void generateMetaCpGs();


        // generate Bit representation of whole genome for genomeBit
        // using full alphabet
        void generateBitStrings(std::vector<std::vector<char> >& genomeSeq);


        // hash all kmers in all CpGs to _kmerTable using ntHash
        // the kmers are represented in REDUCED alphabet {A,T,G}
        // mapping all Cs to Ts
        void generateHashes(std::vector<std::vector<char> >& genomeSeq);


        // generates all kmers in seq and hashes them and their reverse complement using nthash into kmerTable
        // using the reduced alphabet {A,G,T}
        // Arguments:
        //          seq         sequence to look at
        //          lastPos     position of the last kmer hashed inside meta cpg + 1
        //                      THIS VALUE WILL BE UPDATED BY THE FUNCTION
        //          pos         position of CpG in sequence (look at CpG struct for details)
        //          metaCpG     index of meta CpG that we are looking at
        //          metaOff     offset of pos in metaCpG
        //
        inline void ntHashChunk(const std::vector<char>& seq, uint32_t& lastPos, const unsigned int& pos, const uint32_t& metacpg, const uint32_t&& metaOff);
        void ntHashLast(const std::vector<char>& seq, uint32_t& lastPos, const unsigned int& pos, const unsigned int& bpsAfterCpG, const uint32_t& metacpg, uint32_t&& metaOff);
        void ntHashFirst(const std::vector<char>& seq, uint32_t& lastPos, const unsigned int& cpgOffset, const uint32_t& metacpg);



		// preallocates hash table cells by counting k-mer hash values
		// Arguments:
		// 			seq			sequence to look at
        //          lastPos     position of the last kmer hashed inside meta cpg + 1
        //                      THIS VALUE WILL BE UPDATED BY THE FUNCTION
        //          pos         position of CpG in sequence (look at CpG struct for details)
		//          bpsAfterpG	how many base pairs are left after CpG before sequence ends
		//          cpgOffset	how many base pairs until CpG occurs
        inline void ntCountChunk(const std::vector<char>& seq, uint32_t& lastPos, const unsigned int& pos);
        void ntCountLast(std::vector<char>& seq, uint32_t& lastPos, const unsigned int& pos, const unsigned int& bpsAfterCpG);
        void ntCountFirst(std::vector<char>& seq, uint32_t& lastPos, const unsigned int& cpgOffset);

        // estimates the number of collision per entry and number of overall kmers to be hashed
        // to initialize tabIndex and kmerTable
        void estimateTablesizes(std::vector<std::vector<char> >& genomeSeq);


        // blacklist all k-mers that appear more then KMERCUTOFF times in the specified kmerTable slice
        // returns a list of k-mer sequences (as bitstrings) that are blacklisted through argument
        //
        // ARGUMENTS:
        //              KSliceStart Start index where to start blacklisting in KmerTable
        //              KSliceEnd   End index where to stop blacklisting (excluding this index)
        //
        //              blt         map of k-mer strings that are blacklisted
        //                          THIS WILL BE FILLED DURING CALL
        inline void blacklist(const unsigned int& KSliceStart, const unsigned int& KSliceEnd, std::unordered_map<uint64_t, unsigned int>& bl);

        // reproduce the k-mer sequence of a given kmer by looking up the position in the reference genome
        // the sequence will be returned as a bitstring
        //
        // ARGUMENTS:
        //              k       k-mer
        //              sFlag   flag stating if kmer is of forward (true) or reverse (false) strand
        //              kSeq    empty fixed length array to be filled with the kmer sequence
        inline uint64_t reproduceKmerSeq(KMER::kmer& k, bool sFlag);

        // filter the current hash table according to the given blacklist
        // overwrites the internal kmerTable and strandTable structure, as well as tabIndex
        //
        void filterHashTable();
        // filter hash table such that k-mers that occur more then once per meta CpG are deleted
        void filterRedundancyInHashTable();



        // table of all CpGs in reference genome
        std::vector<struct CpG> cpgTable;
        std::vector<struct CpG> cpgStartTable;

        // table of bitstrings* holding a bit representation of genomeSeq
        // used as a perfect hash later on
        // encoding:
        //          A -> 00
        //          C -> 01
        //          T -> 11
        //          G -> 10
        //
        //          N -> 00   DISCARDED for all computations
        //
        // *own implementation of bitstrings
        std::vector<DnaBitStr> genomeBit;
        // full sequence
        std::vector<std::vector<char> > fullSeq;

        // hash table
        // tabIndex [i] points into kmerTable where the first entry with hash value i is saved
        // kmerTable holds the kmer (i.e. MetaCpg index and offset)
        // strandTable hold the strand orientation of the corresponding kmer (true iff forward)
        std::vector<uint64_t> tabIndex;
        std::vector<KMER::kmer> kmerTable;
        std::vector<KMER_S::kmer> kmerTableSmall;
        std::vector<bool> strandTable;
        //
        // meta CpG table
        std::vector<struct metaCpG> metaCpGs;
        std::vector<struct metaCpG> metaStartCpGs;

        struct KmerHash
        {
            // just use the provided kmer representation
            size_t operator() (const uint64_t& k) const {

                return static_cast<size_t>(k);
            }
        };
        // contains all kmers discarded during filterHashTable
        std::unordered_set<uint64_t, KmerHash> filteredKmers;

		// mapping of internal chromosome id to external string identifier from fasta
		std::unordered_map<uint8_t, std::string> chrMap;

};

#endif /* REFGENOME_H */
