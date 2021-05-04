/********************************************************************
 *
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 */

#ifndef ETH_H
#define ETH_H

#include <vector>
#include <univalue.h>
#include <string>

#include "streams.h"
#include "hash.h"
#include "arith_uint256.h"


#include "mmr.h"

class EthereumProof
{
public:
    CPATRICIABranch<CHashWriter> &nPBranch;

    EthereumProof(CPATRICIABranch<CHashWriter> &PBranch) : nPBranch(PBranch) {}

    std::vector<unsigned char> verifyAccountProof(){};
    std::vector<unsigned char> verifyProof(uint256& rootHash,std::vector<unsigned char> key,std::vector<std::vector<unsigned char>>& proof);
    std::vector<unsigned char> EthereumProof::verifyStorageProof(){};
    bool verifyStorageValue(std::vector<unsigned char> testStorageValue){};

};


class RLP {



    public:

    struct rlpDecoded {
        std::vector<std::vector<unsigned char>> data;
        std::vector<unsigned char> remainder; 
    };

    std::vector<unsigned char> encodeLength(int length,int offset){};
    std::vector<unsigned char> encode(std::vector<unsigned char> input){};
    std::vector<unsigned char> encode(std::vector<std::vector<unsigned char>> input){};
    rlpDecoded decode(std::vector<unsigned char> inputBytes){};
    rlpDecoded decode(std::string inputString){};
};

class TrieNode {

    public: 
    enum nodeType{
        BRANCH,
        LEAF,
        EXTENSION
    };
    nodeType type;
    std::vector<std::vector<unsigned char>> raw;
    std::vector<unsigned char> key;
    std::vector<unsigned char> value;

    TrieNode(std::vector<std::vector<unsigned char>> rawNode) {
        raw = rawNode;
        type = setType();
        setKey();
        setValue();
    }



    private: 
    nodeType setType(){}
    void setKey(){}
    void setValue(){}

};



#endif // ETH_H