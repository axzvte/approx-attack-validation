#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


struct PairCandidate
{
    std::string position1;
    std::string unit1;

    int lower1;
    int upper1;


    std::string position2;
    std::string unit2;

    int lower2;
    int upper2;


    double globalPsnr;
    double roiPsnr;
    double gap;
};


// =================================================
// 判断是否包含某个位置
// =================================================

bool containsPosition(
    const PairCandidate& candidate,
    const std::string& position
)
{
    return
        candidate.position1 == position
        ||
        candidate.position2 == position;
}


// =================================================
// 读取CSV文件
// =================================================

std::vector<PairCandidate>
readPairPareto(
    const std::string& filePath
)
{
    std::ifstream file(
        filePath
    );


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open csv file."
        );
    }


    std::vector<PairCandidate>
        candidates;


    std::string line;


    // 跳过表头
    std::getline(
        file,
        line
    );


    while (
        std::getline(
            file,
            line
        )
    )
    {
        std::stringstream ss(line);


        std::string item;


        PairCandidate candidate;


        // index
        std::getline(
            ss,
            item,
            ','
        );


        // position1
        std::getline(
            ss,
            candidate.position1,
            ','
        );


        // unit1
        std::getline(
            ss,
            candidate.unit1,
            ','
        );


        // lower1
        std::getline(
            ss,
            item,
            ','
        );

        candidate.lower1 =
            std::stoi(item);



        // upper1
        std::getline(
            ss,
            item,
            ','
        );

        candidate.upper1 =
            std::stoi(item);



        // position2
        std::getline(
            ss,
            candidate.position2,
            ','
        );



        // unit2
        std::getline(
            ss,
            candidate.unit2,
            ','
        );



        // lower2
        std::getline(
            ss,
            item,
            ','
        );

        candidate.lower2 =
            std::stoi(item);



        // upper2
        std::getline(
            ss,
            item,
            ','
        );

        candidate.upper2 =
            std::stoi(item);



        // global psnr
        std::getline(
            ss,
            item,
            ','
        );

        candidate.globalPsnr =
            std::stod(item);



        // roi psnr
        std::getline(
            ss,
            item,
            ','
        );

        candidate.roiPsnr =
            std::stod(item);



        // gap
        std::getline(
            ss,
            item,
            ','
        );

        candidate.gap =
            std::stod(item);



        candidates.push_back(
            candidate
        );
    }


    return candidates;
}



// =================================================
// main
// =================================================

int main()
{
    try
    {

        const std::string csvPath =
            "results/all_pair_search/"
            "all_pairs_pareto.csv";


        auto candidates =
            readPairPareto(
                csvPath
            );


        std::cout
            << "Total pair candidates: "
            << candidates.size()
            << "\n";


        // ===============================
        // 筛选有效双位置方案
        // ===============================

        std::vector<PairCandidate>
            validCandidates;


        for (
            const auto& candidate :
            candidates
        )
        {
            if (
                candidate.globalPsnr >= 30.0
                &&
                candidate.globalPsnr <= 34.0
            )
            {
                validCandidates.push_back(
                    candidate
                );
            }
        }


        std::cout
            << "Valid candidates: "
            << validCandidates.size()
            << "\n\n";



        // ===============================
        // 统计可扩展三位置组合
        // ===============================

        int a1a2a3 = 0;

        int a1a2a4 = 0;

        int a1a3a4 = 0;

        int a2a3a4 = 0;



        for (
            const auto& candidate :
            validCandidates
        )
        {

            bool hasA1 =
                containsPosition(
                    candidate,
                    "A1"
                );


            bool hasA2 =
                containsPosition(
                    candidate,
                    "A2"
                );


            bool hasA3 =
                containsPosition(
                    candidate,
                    "A3"
                );


            bool hasA4 =
                containsPosition(
                    candidate,
                    "A4"
                );



            // -----------------------------
            // A1+A2 可以扩展：
            // A1+A2+A3
            // A1+A2+A4
            // -----------------------------

            if (
                hasA1
                &&
                hasA2
            )
            {
                a1a2a3++;

                a1a2a4++;
            }



            // -----------------------------
            // A1+A3
            // -----------------------------

            if (
                hasA1
                &&
                hasA3
            )
            {
                a1a2a3++;

                a1a3a4++;
            }



            // -----------------------------
            // A1+A4
            // -----------------------------

            if (
                hasA1
                &&
                hasA4
            )
            {
                a1a2a4++;

                a1a3a4++;
            }



            // -----------------------------
            // A2+A3
            // -----------------------------

            if (
                hasA2
                &&
                hasA3
            )
            {
                a1a2a3++;

                a2a3a4++;
            }



            // -----------------------------
            // A2+A4
            // -----------------------------

            if (
                hasA2
                &&
                hasA4
            )
            {
                a1a2a4++;

                a2a3a4++;
            }



            // -----------------------------
            // A3+A4
            // -----------------------------

            if (
                hasA3
                &&
                hasA4
            )
            {
                a1a3a4++;

                a2a3a4++;
            }

        }



        std::cout
            << "Possible triple extensions:\n";


        std::cout
            << "A1+A2+A3 : "
            << a1a2a3
            << "\n";


        std::cout
            << "A1+A2+A4 : "
            << a1a2a4
            << "\n";


        std::cout
            << "A1+A3+A4 : "
            << a1a3a4
            << "\n";


        std::cout
            << "A2+A3+A4 : "
            << a2a3a4
            << "\n";


        return 0;

    }
    catch(
        const std::exception& e
    )
    {
        std::cerr
            << "Error: "
            << e.what()
            << "\n";

        return 1;
    }
}