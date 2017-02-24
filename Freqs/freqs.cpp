#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <memory>

namespace encoding
{

bool InterpretFirstUtfByte(uint8_t byte, size_t* bytesCount, uint32_t* letter)
{
    if((byte & 128u) == 0)
    {
        *bytesCount = 1;
        *letter = static_cast<uint32_t>(byte);

        return true;
    }

    if((byte & 128u) == 0)
    {
        // Invalid format
        return false;
    }

    int byteIndex = 5;

    while(byteIndex > 1)
    {
        if((byte & (1u << byteIndex)) == 0)
        {
            *bytesCount = 7 - byteIndex;
            *letter = byte & (255u >> (8 - byteIndex));

            return true;
        }

        --byteIndex;
    }

    return true;
}

bool InterpretNextUtfByte(uint32_t* letter, uint8_t byte)
{
    // Invalid format check
    if((byte & 128u) == 0 ||
        (byte & 64u) != 0)
    {
        return false;
    }

    *letter = ((*letter) << 6) | (byte & (255u >> 2));

    return true;
}

bool ReadUtfLetter(uint8_t* buffer, size_t bufferSize, size_t* startIndex, uint32_t* letter)
{
    if(*startIndex >= bufferSize)
    {
        return false;
    }

    size_t bytesCount;

    if(!InterpretFirstUtfByte(buffer[*startIndex], &bytesCount, letter))
    {
        return false;
    }

    while(bytesCount-- > 1)
    {
        if(++(*startIndex) >= bufferSize)
        {
            return false;
        }

        if(!InterpretNextUtfByte(letter, buffer[*startIndex]))
        {
            return false;
        }
    }

    ++(*startIndex);

    return true;
}

class LetterInfo
{
public:
    size_t bufferOffset;
    uint8_t bytesCount;
};

bool ReadUtfLetters(uint8_t* buffer, size_t bufferSize, std::vector<uint32_t>* letters, std::vector<LetterInfo>* letterInfos)
{
    size_t byteIndex = 0;

    while(byteIndex < bufferSize)
    {
        uint32_t letter;
        const auto prevByteIndex = byteIndex;

        if(!ReadUtfLetter(buffer, bufferSize, &byteIndex, &letter))
        {
            return false;
        }

        letters->push_back(letter);

        if(letterInfos != nullptr)
        {
            LetterInfo li;
            li.bufferOffset = prevByteIndex;
            li.bytesCount = static_cast<uint8_t>(byteIndex - prevByteIndex);

            letterInfos->push_back(li);
        }
    }

    return true;
}

}

bool ReadStreamToBuffer(std::istream& input, std::unique_ptr<uint8_t[]>* resultBuffer, size_t* bufferSize)
{
    // Set pos to the end to get file size
    input.seekg(0, std::ios::end);

    *bufferSize = static_cast<size_t>(input.tellg());
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[*bufferSize]);

    // Set pos back to the beginning
    input.seekg(0, std::ios::beg);

    input.read((char*)buffer.get(), *bufferSize);

    if(input.bad() || input.fail() || input.eof())
    {
        return false;
    }

    *resultBuffer = std::move(buffer);

    return true;
}

enum class ExitCode : int
{
    NoError = 0,
    InvalidInputArgsCount,
    InvalidInputFile,
    InvalidOutputFile,
    InvalidFileFormat
};

ExitCode Main(int argc, const char** argv)
{
    if(argc != 3)
    {
        return ExitCode::InvalidInputArgsCount;
    }

    auto inputFileName = argv[1];
    std::ifstream input(inputFileName, std::ios::in | std::ios::binary);

    if(!input.is_open())
    {
        return ExitCode::InvalidInputFile;
    }

    auto outputFileName = argv[2];
    std::ofstream output(outputFileName);

    if(!output.is_open())
    {
        return ExitCode::InvalidOutputFile;
    }

    size_t bufferSize;
    std::unique_ptr<uint8_t[]> buffer;

    if(!ReadStreamToBuffer(input, &buffer, &bufferSize))
    {
        return ExitCode::InvalidInputFile;
    }

    std::vector<uint32_t> letters;
    std::vector<encoding::LetterInfo> lettersInfos;
    if(!encoding::ReadUtfLetters(buffer.get(), bufferSize, &letters, &lettersInfos))
    {
        return ExitCode::InvalidFileFormat;
    }

    class Alphabet
    {
    public:
        uint32_t upperCaseBegin;
        uint32_t lowerCaseBegin;
        uint32_t lowerCaseEnd;
    };

    /* Those alphabets are supposed to be sorted by first letters.
     * I need only two here, so I will init them by myself
     */
    const Alphabet alphabets[]
    {
        // English
        { 65, 97, 122 },

        // Russian
        { 1040, 1072, 1104 }
    };

    auto IsAlpha = [&alphabets](uint32_t letter)
    {
        for(const auto& a : alphabets)
        {
            if(letter < a.upperCaseBegin)
            {
                return false;
            }

            if(letter < a.lowerCaseEnd)
            {
                return true;
            }
        }

        return false;
    };

    // Convert letters to lower case to compare them properly
    for(auto& letter : letters)
    {
        for(const auto& a : alphabets)
        {
            if(letter >= a.upperCaseBegin && letter < a.lowerCaseBegin)
            {
                letter += a.lowerCaseBegin - a.upperCaseBegin;

                break;
            }
        }
    }

    class WordInfo
    {
    public:
        size_t startLetterIndex;
        size_t lettersCount;
        size_t entries;
    };

    std::vector<WordInfo> words;

    // Fill words collection here
    {
        auto registerWord = [&words, &letters](size_t firstLetter, size_t lettersCount)
        {
            // Find duplicates
            for(auto& word : words)
            {
                if(word.lettersCount != lettersCount)
                {
                    continue;
                }

                bool theSame = true;

                for(size_t letterIndexInWord = 0; letterIndexInWord < word.lettersCount; ++letterIndexInWord)
                {
                    if(letters[firstLetter + letterIndexInWord] !=
                        letters[word.startLetterIndex + letterIndexInWord])
                    {
                        theSame = false;
                        break;
                    }
                }

                if(theSame)
                {
                    ++word.entries;
                    return;
                }
            }

            WordInfo word;
            word.startLetterIndex = firstLetter;
            word.lettersCount = lettersCount;
            word.entries = 1;

            words.push_back(word);
        };

        int firstLetter = -1;

        // Gather words
        for(size_t letterIndex = 0; letterIndex < letters.size(); ++letterIndex)
        {
            if(IsAlpha(letters[letterIndex]))
            {
                if(firstLetter == -1)
                {
                    firstLetter = static_cast<int>(letterIndex);
                }
            }
            else if(firstLetter != -1)
            {
                registerWord(firstLetter, letterIndex - firstLetter);

                firstLetter = -1;
            }
        }

        // Last word
        if(firstLetter != -1)
        {
            registerWord(firstLetter, letters.size() - firstLetter);
        }
    }

    // Sort words before output
    std::sort(words.begin(), words.end(),
        [&letters](const WordInfo& a, const WordInfo& b)
    {
        if(a.entries == b.entries)
        {
            const auto pLetters = letters.data();
            const auto aBegin = pLetters + a.startLetterIndex;
            const auto bBegin = pLetters + b.startLetterIndex;

            return std::lexicographical_compare(
                aBegin, aBegin + a.lettersCount,
                bBegin, bBegin + b.lettersCount);
        }

        return a.entries > b.entries;
    });

    // Write result to the ouput file
    for(const auto& word : words)
    {
        output << word.entries << ' ';

        for(size_t letterIndexInWord = 0; letterIndexInWord < word.lettersCount; ++letterIndexInWord)
        {
            const auto& letterInfo = lettersInfos[word.startLetterIndex + letterIndexInWord];

            output.write(
                (const char*)buffer.get() + letterInfo.bufferOffset,
                letterInfo.bytesCount
            );
        }

        output << std::endl;
    }

    output.close();

    return ExitCode::NoError;
}

int main(int argc, const char** argv)
{
    return static_cast<int>(Main(argc, argv));
}