/*!
 * @file bioparser.hpp
 *
 * @brief Bioparser header file
 */

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

namespace bioparser {

constexpr uint32_t kBufferSize = 64 * 1024;

// Small/Medium/Large Storage Size
constexpr uint32_t kSSS = 1024;
constexpr uint32_t kMSS = 8 * 1024 * 1024;
constexpr uint32_t kLSS = 512 * 1024 * 1024;

/*!
 * @brief Parser absctract class
 */
template<class T>
class Parser;

template<template<class> class P, class T>
std::unique_ptr<Parser<T>> createParser(const std::string& path);

/*!
 * @brief Parser specializations
 */
template<class T>
class FastaParser;

template<class T>
class FastqParser;

template<class T>
class MhapParser;

template<class T>
class PafParser;

/*!
 * @brief Parser definitions
 */
template<class T>
class Parser {
public:
    virtual ~Parser() = 0;

    void reset();

    virtual bool parse_objects(std::vector<std::unique_ptr<T>>& dst,
        uint64_t max_bytes) = 0;

    bool parse_objects(std::vector<std::shared_ptr<T>>& dst, uint64_t max_bytes);
protected:
    Parser(FILE* input_file, uint32_t storage_size);
    Parser(const Parser&) = delete;
    const Parser& operator=(const Parser&) = delete;

    std::unique_ptr<FILE, int(*)(FILE*)> input_file_;
    std::vector<char> buffer_;
    std::vector<char> storage_;
};

template<class T>
class FastaParser: public Parser<T> {
public:
    ~FastaParser();

    bool parse_objects(std::vector<std::unique_ptr<T>>& dst,
        uint64_t max_bytes) override;

    friend std::unique_ptr<Parser<T>>
        createParser<bioparser::FastaParser, T>(const std::string& path);
private:
    FastaParser(FILE* input_file);
    FastaParser(const FastaParser&) = delete;
    const FastaParser& operator=(const FastaParser&) = delete;
};

template<class T>
class FastqParser: public Parser<T> {
public:
    ~FastqParser();

    bool parse_objects(std::vector<std::unique_ptr<T>>& dst,
        uint64_t max_bytes) override;

    friend std::unique_ptr<Parser<T>>
        createParser<bioparser::FastqParser, T>(const std::string& path);
private:
    FastqParser(FILE* input_file);
    FastqParser(const FastqParser&) = delete;
    const FastqParser& operator=(const FastqParser&) = delete;
};

template<class T>
class MhapParser: public Parser<T> {
public:
    ~MhapParser();

    bool parse_objects(std::vector<std::unique_ptr<T>>& dst,
        uint64_t max_bytes) override;

    friend std::unique_ptr<Parser<T>>
        createParser<bioparser::MhapParser, T>(const std::string& path);
private:
    MhapParser(FILE* input_file);
    MhapParser(const MhapParser&) = delete;
    const MhapParser& operator=(const MhapParser&) = delete;
};

template<class T>
class PafParser: public Parser<T> {
public:
    ~PafParser();

    bool parse_objects(std::vector<std::unique_ptr<T>>& dst,
        uint64_t max_bytes) override;

    friend std::unique_ptr<Parser<T>>
        createParser<bioparser::PafParser, T>(const std::string& path);
private:
    PafParser(FILE* input_file);
    PafParser(const PafParser&) = delete;
    const PafParser& operator=(const PafParser&) = delete;
};

/*!
 * @brief Implementation
 */

template<class T>
Parser<T>::Parser(FILE* input_file, uint32_t storage_size)
        : input_file_(input_file, fclose), buffer_(kBufferSize, 0),
        storage_(storage_size, 0) {
}

template<class T>
Parser<T>::~Parser() {
}

template<class T>
void Parser<T>::reset() {
    fseek(this->input_file_.get(), 0, SEEK_SET);
}

template<class T>
bool Parser<T>::parse_objects(std::vector<std::shared_ptr<T>>& dst,
    uint64_t max_bytes) {

    std::vector<std::unique_ptr<T>> tmp;
    auto ret = this->parse_objects(tmp, max_bytes);

    dst.reserve(dst.size() + tmp.size());
    for (auto& it: tmp) {
        dst.emplace_back(std::move(it));
    }
    return ret;
}

template<template<class> class P, class T>
std::unique_ptr<Parser<T>> createParser(const std::string& path) {

    auto input_file = fopen(path.c_str(), "r");
    if (input_file == nullptr) {
        fprintf(stderr, "bioparser::createParser error: "
            "unable to open file %s!\n", path.c_str());
        exit(1);
    }

    return std::unique_ptr<Parser<T>>(new P<T>(input_file));
}

template<class T>
FastaParser<T>::FastaParser(FILE* input_file)
        : Parser<T>(input_file, kSSS + kMSS) {
}

template<class T>
FastaParser<T>::~FastaParser() {
}

template<class T>
bool FastaParser<T>::parse_objects(std::vector<std::unique_ptr<T>>& dst,
    uint64_t max_bytes) {

    auto input_file = this->input_file_.get();
    bool is_end = feof(input_file);
    bool is_valid = false;
    bool status = false;
    uint64_t current_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t num_objects = 0;
    uint64_t last_object_id = num_objects;
    uint32_t line_number = 0;

    char* name = &(this->storage_[0]);
    uint32_t name_length = 0;

    char* data = &(this->storage_[kSSS]);
    uint32_t data_length = 0;

    while (!is_end) {

        uint64_t read_bytes = fread(this->buffer_.data(), sizeof(char),
            this->buffer_.size(), input_file);
        is_end = feof(input_file);

        total_bytes += read_bytes;
        if (max_bytes != 0 && total_bytes > max_bytes) {
            if (last_object_id == num_objects) {
                fprintf(stderr, "bioparser::FastaParser error: "
                    "too small chunk size!\n");
                exit(1);
            }
            fseek(input_file, -(current_bytes + read_bytes), SEEK_CUR);
            status = true;
            break;
        }

        for (uint32_t i = 0; i < read_bytes; ++i) {
            auto c = this->buffer_[i];

            if (c == '\n') {
                ++line_number;
                if (is_end && i == read_bytes - 1) {
                    is_valid = true;
                }
            } else if (c == '>' && line_number != 0) {
                is_valid = true;
                line_number = 0;
            } else {
                switch (line_number) {
                    case 0:
                        if (name_length < kSSS) {
                            if (!(name_length == 0 && isspace(c))) {
                                name[name_length++] = c;
                            }
                        }
                        break;
                    default:
                        data[data_length++] = c;
                        if (data_length == kMSS) {
                            this->storage_.resize(kSSS + kLSS, 0);
                            name = &(this->storage_[0]);
                            data = &(this->storage_[kSSS]);
                        }
                        break;
                }
            }

            ++current_bytes;

            if (is_valid) {
                while (name_length > 0 && isspace(name[name_length - 1])) {
                    --name_length;
                }
                while (data_length > 0 && isspace(data[data_length - 1])) {
                    --data_length;
                }

                if (name_length == 0 || name[0] != '>' || data_length == 0) {
                    fprintf(stderr, "bioparser::FastaParser error: "
                        "invalid file format!\n");
                    exit(1);
                }

                dst.emplace_back(std::unique_ptr<T>(new T(
                    (const char*) &(name[1]), name_length - 1,
                    (const char*) data, data_length)));

                ++num_objects;
                current_bytes = 1;
                name_length = 1;
                data_length = 0;
                is_valid = false;
            }
        }
    }

    return status;
}

template<class T>
FastqParser<T>::FastqParser(FILE* input_file)
        : Parser<T>(input_file, kSSS + 2 * kMSS) {
}

template<class T>
FastqParser<T>::~FastqParser() {
}

template<class T>
bool FastqParser<T>::parse_objects(std::vector<std::unique_ptr<T>>& dst,
    uint64_t max_bytes) {

    auto input_file = this->input_file_.get();
    bool is_end = feof(input_file);
    bool is_valid = false;
    bool status = false;
    uint64_t current_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t num_objects = 0;
    uint64_t last_object_id = num_objects;
    uint32_t line_number = 0;

    char* name = &(this->storage_[0]);
    uint32_t name_length = 0;

    char* data = &(this->storage_[kSSS]);
    uint32_t data_length = 0;

    char* quality = &(this->storage_[kSSS + kMSS]);
    uint32_t quality_length = 0;

    while (!is_end) {

        uint64_t read_bytes = fread(this->buffer_.data(), sizeof(char),
            this->buffer_.size(), input_file);
        is_end = feof(input_file);

        total_bytes += read_bytes;
        if (max_bytes != 0 && total_bytes > max_bytes) {
            if (last_object_id == num_objects) {
                fprintf(stderr, "bioparser::FastqParser error: "
                    "too small chunk size!\n");
                exit(1);
            }
            fseek(input_file, -(current_bytes + read_bytes), SEEK_CUR);
            status = true;
            break;
        }

        for (uint32_t i = 0; i < read_bytes; ++i) {
            auto c = this->buffer_[i];

            if (c == '\n') {
                line_number = (line_number + 1) % 4;
                if (line_number == 0 || (is_end && i == read_bytes - 1)) {
                    is_valid = true;
                }
            } else {
                switch (line_number) {
                    case 0:
                        if (name_length < kSSS) {
                            if (!(name_length == 0 && isspace(c))) {
                                name[name_length++] = c;
                            }
                        }
                        break;
                    case 1:
                        data[data_length++] = c;
                        if (data_length == kMSS) {
                            this->storage_.resize(kSSS + 2 * kLSS, 0);
                            name = &(this->storage_[0]);
                            data = &(this->storage_[kSSS]);
                            quality = &(this->storage_[kSSS + kLSS]);
                        }
                        break;
                    case 2:
                        // comment line starting with '+'
                        // do nothing
                        break;
                    case 3:
                        quality[quality_length++] = c;
                        break;
                    default:
                        // never reaches this case
                        break;
                }
            }

            ++current_bytes;

            if (is_valid) {

                while (name_length > 0 && isspace(name[name_length - 1])) {
                    --name_length;
                }
                while (data_length > 0 && isspace(data[data_length - 1])) {
                    --data_length;
                }
                while (quality_length > 0 && isspace(quality[quality_length - 1])) {
                    --quality_length;
                }

                if (name_length == 0 || name[0] != '@' || data_length == 0 ||
                    quality_length == 0 || data_length != quality_length) {
                    fprintf(stderr, "bioparser::FastqParser error: "
                        "invalid file format!\n");
                    exit(1);
                }

                dst.emplace_back(std::unique_ptr<T>(new T(
                    (const char*) &(name[1]), name_length - 1,
                    (const char*) data, data_length,
                    (const char*) quality, quality_length)));

                ++num_objects;
                current_bytes = 0;
                name_length = 0;
                data_length = 0;
                quality_length = 0;
                is_valid = false;
            }
        }
    }

    return status;
}

template<class T>
MhapParser<T>::MhapParser(FILE* input_file)
        : Parser<T>(input_file, kSSS) {
}

template<class T>
MhapParser<T>::~MhapParser() {
}

template<class T>
bool MhapParser<T>::parse_objects(std::vector<std::unique_ptr<T>>& dst,
    uint64_t max_bytes) {

    auto input_file = this->input_file_.get();
    bool is_end = feof(input_file);
    bool is_valid = false;
    bool status = false;
    uint64_t current_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t num_objects = 0;
    uint64_t last_object_id = num_objects;
    uint32_t line_number = 0;

    const uint32_t kMhapObjectLength = 12;
    uint32_t values_length = 0;

    char* line = &(this->storage_[0]);
    uint32_t line_length = 0;

    uint32_t a_id = 0, a_rc = 0, a_begin = 0, a_end = 0, a_length = 0,
        b_id = 0, b_rc = 0, b_begin = 0, b_end = 0, b_length = 0,
        minmers = 0;
    double error = 0;

    while (!is_end) {

        uint64_t read_bytes = fread(this->buffer_.data(), sizeof(char),
            this->buffer_.size(), input_file);
        is_end = feof(input_file);

        total_bytes += read_bytes;
        if (max_bytes != 0 && total_bytes > max_bytes) {
            if (last_object_id == num_objects) {
                fprintf(stderr, "bioparser::MhapParser error: "
                    "too small chunk size!\n");
                exit(1);
            }
            fseek(input_file, -(current_bytes + read_bytes), SEEK_CUR);
            status = true;
            break;
        }

        for (uint32_t i = 0; i < read_bytes; ++i) {

            auto c = this->buffer_[i];
            ++current_bytes;

            if (c == '\n') {

                line[line_length] = 0;
                while (line_length > 0 && isspace(line[line_length - 1])) {
                    line[line_length - 1] = 0;
                    --line_length;
                }

                uint32_t begin = 0;
                while (true) {
                    uint32_t end = begin;
                    for (uint32_t j = begin; j < line_length; ++j) {
                        if (line[j] == ' ') {
                            end = j;
                            break;
                        }
                    }
                    if (end == begin) {
                        end = line_length;
                    }
                    line[end] = 0;

                    switch (values_length) {
                        case 0:
                            a_id = atoi(&line[begin]);
                            break;
                        case 1:
                            b_id = atoi(&line[begin]);
                            break;
                        case 2:
                            error = atof(&line[begin]);
                            break;
                        case 3:
                            minmers = atoi(&line[begin]);
                            break;
                        case 4:
                            a_rc = atoi(&line[begin]);
                            break;
                        case 5:
                            a_begin = atoi(&line[begin]);
                            break;
                        case 6:
                            a_end = atoi(&line[begin]);
                            break;
                        case 7:
                            a_length = atoi(&line[begin]);
                            break;
                        case 8:
                            b_rc = atoi(&line[begin]);
                            break;
                        case 9:
                            b_begin = atoi(&line[begin]);
                            break;
                        case 10:
                            b_end = atoi(&line[begin]);
                            break;
                        case 11:
                        default:
                            b_length = atoi(&line[begin]);
                            break;
                    }
                    values_length++;
                    if (end == line_length || values_length == kMhapObjectLength) {
                        break;
                    }
                    begin = end + 1;
                }

                if (values_length != kMhapObjectLength) {
                    fprintf(stderr, "bioparser::MhapParser error: "
                        "invalid file format!\n");
                    exit(1);
                }

                dst.emplace_back(std::unique_ptr<T>(new T(a_id, b_id, error,
                    minmers, a_rc, a_begin, a_end, a_length, b_rc, b_begin,
                    b_end, b_length)));

                ++num_objects;
                current_bytes = 0;
                line_length = 0;
                values_length = 0;
            } else {
                line[line_length++] = c;
            }
        }
    }

    return status;
}

template<class T>
PafParser<T>::PafParser(FILE* input_file)
        : Parser<T>(input_file, 3 * kSSS) {
}

template<class T>
PafParser<T>::~PafParser() {
}

template<class T>
bool PafParser<T>::parse_objects(std::vector<std::unique_ptr<T>>& dst,
    uint64_t max_bytes) {

    auto input_file = this->input_file_.get();
    bool is_end = feof(input_file);
    bool is_valid = false;
    bool status = false;
    uint64_t current_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t num_objects = 0;
    uint64_t last_object_id = num_objects;
    uint32_t line_number = 0;

    const uint32_t kPafObjectLength = 12;
    uint32_t values_length = 0;

    char* line = &(this->storage_[0]);
    uint32_t line_length = 0;

    const char* a_name = nullptr, * b_name = nullptr;

    uint32_t a_name_length = 0, a_length = 0, a_begin = 0, a_end = 0,
        b_name_length = 0, b_length = 0, b_begin = 0, b_end = 0,
        matching_bases = 0, overlap_length = 0, quality = 0;
    char orientation = '\0';

    while (!is_end) {

        uint64_t read_bytes = fread(this->buffer_.data(), sizeof(char),
            this->buffer_.size(), input_file);
        is_end = feof(input_file);

        total_bytes += read_bytes;
        if (max_bytes != 0 && total_bytes > max_bytes) {
            if (last_object_id == num_objects) {
                fprintf(stderr, "bioparser::PafParser error: "
                    "too small chunk size!\n");
                exit(1);
            }
            fseek(input_file, -(current_bytes + read_bytes), SEEK_CUR);
            status = true;
            break;
        }

        for (uint32_t i = 0; i < read_bytes; ++i) {

            auto c = this->buffer_[i];
            ++current_bytes;

            if (c == '\n') {

                line[line_length] = 0;
                while (line_length > 0 && isspace(line[line_length - 1])) {
                    line[line_length - 1] = 0;
                    --line_length;
                }

                uint32_t begin = 0;
                while (true) {
                    uint32_t end = begin;
                    for (uint32_t j = begin; j < line_length; ++j) {
                        if (line[j] == '\t') {
                            end = j;
                            break;
                        }
                    }
                    if (end == begin) {
                        end = line_length;
                    }
                    line[end] = 0;

                    switch (values_length) {
                        case 0:
                            a_name = &line[begin];
                            a_name_length = end - begin;
                            break;
                        case 1:
                            a_length = atoi(&line[begin]);
                            break;
                        case 2:
                            a_begin = atoi(&line[begin]);
                            break;
                        case 3:
                            a_end = atoi(&line[begin]);
                            break;
                        case 4:
                            orientation = line[begin];
                            break;
                        case 5:
                            b_name = &line[begin];
                            b_name_length = end - begin;
                            break;
                        case 6:
                            b_length = atoi(&line[begin]);
                            break;
                        case 7:
                            b_begin = atoi(&line[begin]);
                            break;
                        case 8:
                            b_end = atoi(&line[begin]);
                            break;
                        case 9:
                            matching_bases = atoi(&line[begin]);
                            break;
                        case 10:
                            overlap_length = atoi(&line[begin]);
                            break;
                        case 11:
                        default:
                            quality = atoi(&line[begin]);
                            break;
                    }
                    values_length++;
                    if (end == line_length || values_length == kPafObjectLength) {
                        break;
                    }
                    begin = end + 1;
                }

                while (a_name_length > 0 && isspace(a_name[a_name_length - 1])) {
                    --a_name_length;
                }
                a_name_length = std::min(a_name_length, kSSS);

                while (b_name_length > 0 && isspace(b_name[b_name_length - 1])) {
                    --b_name_length;
                }
                b_name_length = std::min(b_name_length, kSSS);

                if (a_name_length == 0 || b_name_length == 0 ||
                    values_length != kPafObjectLength) {

                    fprintf(stderr, "bioparser::PafParser error: "
                        "invalid file format!\n");
                    exit(1);
                }

                dst.emplace_back(std::unique_ptr<T>(new T(a_name, a_name_length,
                    a_length, a_begin, a_end, orientation, b_name, b_name_length,
                    b_length, b_begin, b_end, matching_bases, overlap_length,
                    quality)));

                ++num_objects;
                current_bytes = 0;
                line_length = 0;
                values_length = 0;
            } else {
                line[line_length++] = c;
            }
        }
    }

    return status;
}

}
