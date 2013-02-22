// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cstdio>
#include <istream>
#include <iomanip>
#include <sstream>

#include <openssl/md5.h>

#include "utils.h"

uint64 Md5Prefix(const std::string& input) {
  uint64 digest_prefix = 0;
  unsigned char digest[MD5_DIGEST_LENGTH + 1];

  MD5(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(),
      digest);
  // We need 64-bits / # of bits in a byte.
  std::stringstream ss;
  for( size_t i = 0 ; i < sizeof(uint64) ; i++ )
    // The setw(2) and setfill('0') calls are needed to make sure we output 2
    // hex characters for every 8-bits of the hash.
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned int>(digest[i]);
  ss >> digest_prefix;
  return digest_prefix;
}

std::ifstream::pos_type GetFileSize(const std::string& filename) {
  std::ifstream in(filename.c_str(), std::ifstream::in | std::ifstream::binary);
  in.seekg(0, std::ifstream::end);
  return in.tellg();
}

bool BufferToFile(const std::string& filename,
                  const std::vector<char> & contents) {
  std::ofstream out(filename.c_str(), std::ios::binary);
  if (out.good())
  {
    out.write(&contents[0], contents.size() * sizeof(contents[0]));
    out.close();
    if (out.good())
      return true;
  }
  return false;
}

bool FileToBuffer(const std::string& filename, std::vector<char>* contents) {
  contents->reserve(GetFileSize(filename));
  std::ifstream in(filename.c_str(), std::ios::binary);
  if (in.good())
  {
    contents->assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
    if (in.good())
      return true;
  }
  return false;
}

bool CompareFileContents(const std::string& a, const std::string& b) {
  struct FileInfo {
    std::string name;
    std::vector<char> contents;
  };
  FileInfo file_infos[2];
  file_infos[0].name = a;
  file_infos[1].name = b;

  for ( size_t i = 0 ; i < sizeof(file_infos)/sizeof(file_infos[0]) ; i++ ) {
    if(!FileToBuffer(file_infos[i].name, &file_infos[i].contents))
      return false;
  }

  return file_infos[0].contents == file_infos[1].contents;
}

bool CreateNamedTempFile(std::string * name) {
  char filename[] = "/tmp/XXXXXX";
  int fd = mkstemp(filename);
  if (fd == -1)
    return false;
  close(fd);
  *name = filename;
  return true;
}
