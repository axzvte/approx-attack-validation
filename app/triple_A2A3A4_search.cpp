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



struct TripleBase
{
    PairCandidate pair;

    std::string addedPosition;
};



bool isA2A3(
    const PairCandidate& candidate
)
{
    return
        (
            candidate.position1 == "A2"
            &&
            candidate.position2 == "A3"
        )
        ||
        (
            candidate.position1 == "A3"
            &&
            candidate.position2 == "A2"
        );
}



std::vector<PairCandidate>
readPairCSV(
    const std::string& path
)
{
    std::ifstream file(path);


    if (!file.is_open())
    {
        throw std::runtime_error(
            "Cannot open csv."
        );
    }


    std::vector<PairCandidate>
        result;


    std::string line;


    std::getline(
        file,
        line
    );


    while(
        std::getline(
            file,
            line
        )
    )
    {
        std::stringstream ss(line);


        std::string item;


        PairCandidate c;


        // index
        std::getline(ss,item,',');


        std::getline(
            ss,
            c.position1,
            ','
        );


        std::getline(
            ss,
            c.unit1,
            ','
        );


        std::getline(
            ss,
            item,
            ','
        );

        c.lower1 =
            std::stoi(item);



        std::getline(
            ss,
            item,
            ','
        );

        c.upper1 =
            std::stoi(item);



        std::getline(
            ss,
            c.position2,
            ','
        );


        std::getline(
            ss,
            c.unit2,
            ','
        );


        std::getline(
            ss,
            item,
            ','
        );

        c.lower2 =
            std::stoi(item);



        std::getline(
            ss,
            item,
            ','
        );

        c.upper2 =
            std::stoi(item);



        std::getline(
            ss,
            item,
            ','
        );

        c.globalPsnr =
            std::stod(item);



        std::getline(
            ss,
            item,
            ','
        );

        c.roiPsnr =
            std::stod(item);



        std::getline(
            ss,
            item,
            ','
        );

        c.gap =
            std::stod(item);



        result.push_back(c);
    }


    return result;
}



int main()
{

    try
    {

        auto candidates =
            readPairCSV(
                "results/all_pair_search/"
                "all_pairs_pareto.csv"
            );


        std::cout
            << "Total candidates: "
            << candidates.size()
            << "\n";



        std::vector<PairCandidate>
            a2a3Candidates;



        for(
            const auto& c:
            candidates
        )
        {

            if(
                c.globalPsnr >=30.0
                &&
                c.globalPsnr <=34.0
                &&
                isA2A3(c)
            )
            {
                a2a3Candidates.push_back(c);
            }
        }



        std::cout
            << "A2+A3 candidates: "
            << a2a3Candidates.size()
            << "\n\n";



        for(
            int i=0;
            i<
            std::min(
                5,
                static_cast<int>(
                    a2a3Candidates.size()
                )
            );
            i++
        )
        {

            const auto& c =
                a2a3Candidates[i];


            std::cout
                << "Example "
                << i+1
                << "\n";


            std::cout
                << c.position1
                << ": "
                << c.unit1
                << " ["
                << c.lower1
                << ","
                << c.upper1
                << "]\n";


            std::cout
                << c.position2
                << ": "
                << c.unit2
                << " ["
                << c.lower2
                << ","
                << c.upper2
                << "]\n";


            std::cout
                << "Global:"
                << c.globalPsnr
                << "\n";


            std::cout
                << "ROI:"
                << c.roiPsnr
                << "\n\n";
        }


        return 0;

    }
    catch(
        const std::exception& e
    )
    {
        std::cerr
            << e.what()
            << "\n";

        return 1;
    }
}