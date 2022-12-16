// $Id: cxi.cpp,v 1.5 2021-05-18 01:32:29-07 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include "debug.h"
#include "logstream.h"
#include "protocol.h"
#include "socket.h"

logstream outlog (cout);
struct cxi_exit: public exception {};

unordered_map<string,cxi_command> command_map {
   {"exit", cxi_command::EXIT},
   {"help", cxi_command::HELP},
   {"ls"  , cxi_command::LS  },
   {"get" , cxi_command::GET },
   {"put" , cxi_command::PUT  },
   {"rm"  , cxi_command::RM   },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cxi_help() {
   cout << help;
}

void cxi_ls (client_socket& server) {
   cxi_header header;
   header.command = cxi_command::LS;
   //outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   //outlog << "received header " << header << endl;
   if (header.command != cxi_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else {
      size_t host_nbytes = ntohl (header.nbytes);
      auto buffer = make_unique<char[]> (host_nbytes + 1);
      recv_packet (server, buffer.get(), host_nbytes);
      //outlog << "received " << host_nbytes << " bytes" << endl;
      buffer[host_nbytes] = '\0';
      cout << buffer.get();
   }
}

void cxi_get (client_socket& server, string filename) {
   cxi_header header;
   header.command = cxi_command::GET;
   snprintf(header.filename, filename.length() + 1,
      filename.c_str());
   //outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   //outlog << "received header " << header << endl;
   if (header.command != cxi_command::FILEOUT) {
      outlog << filename << " is not a file" << endl;
      outlog << "server returned " << header << endl;
   }
   else {
      char *buffer = new char[header.nbytes + 1];
      //size_t host_nbytes = ntohl (header.nbytes);
      //auto buffer = make_unique<char[]> (host_nbytes + 1);
      //I tried to use ntohl, but for some reason I got segmentation
      //faults, so I just allocated an array dynamically
      recv_packet (server, buffer, header.nbytes);
      //outlog << "received " << host_nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      ofstream os (header.filename, std::ofstream::binary);
      os.write(buffer, header.nbytes);
      outlog << filename << " was successfully added" << endl;
      delete[] buffer;
      os.close();
   }
}

void cxi_put (client_socket& server, string filename) {
   cxi_header header;
   snprintf(header.filename, filename.length() + 1, 
      filename.c_str());
   //copying filename
   ifstream file (filename, ifstream::binary);
   if (file.is_open()) {
      file.seekg(0, file.end);
      int length = file.tellg();
      file.seekg(0, file.beg);
      char *buffer = new char[length]; //C++ doesn't allow 
      //to create dynamic arrays on the stack
      file.read(buffer, length);
      header.command = cxi_command::PUT;
      header.nbytes = length;
      send_packet (server, &header, sizeof header);
      send_packet (server, buffer, length);
      recv_packet (server, &header, sizeof header);
      delete[] buffer; //clearing allocated memory
   }
   else {
      cerr << "Error message: could not find file: " 
      << filename << endl;
   }
   if (header.command == cxi_command::ACK) {
      cout << "ACK received: put file on the server" << endl;
   }
   else if (header.command == cxi_command::NAK) {
      cerr << "NAK received: couldn't put file on the server" 
      << endl;
   }
   file.close();
}

void cxi_rm (client_socket& server, string filename) {
   cxi_header header;
   snprintf(header.filename, filename.length() + 1, 
      filename.c_str());
   header.command = cxi_command::RM;
   header.nbytes = 0;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);

   if (header.command == cxi_command::ACK) {
      cout << "ACK received: file delete successfully: " << 
      filename << endl;
   }
   else if (header.command == cxi_command::NAK) {
      cerr << "NAK received: couldn't delete the file: " <<
      filename << endl;
   }
}


void usage() {
   cerr << "Usage: " << outlog.execname() << " host port" << endl;
   throw cxi_exit();
}

pair<string,in_port_t> scan_options (int argc, char** argv) {
   for (;;) {
      int opt = getopt (argc, argv, "@:");
      if (opt == EOF) break;
      switch (opt) {
         case '@': debugflags::setflags (optarg);
                   break;
      }
   }
   if (argc - optind != 2) usage();
   string host = argv[optind];
   in_port_t port = get_cxi_server_port (argv[optind + 1]);
   return {host, port};
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << to_string (hostinfo()) << endl;
   try {
      auto host_port = scan_options (argc, argv);
      string host = host_port.first;
      in_port_t port = host_port.second;
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cxi_exit();
         vector<string> string2;
         istringstream ss(line);
         string token;
         while (getline(ss, token, ' ')) {
            string2.push_back(token);
         }
         outlog << "command " << line << endl;
         const auto& itor = command_map.find (string2[0]);
         cxi_command cmd = itor == command_map.end()
                         ? cxi_command::ERROR : itor->second;
         switch (cmd) {
            case cxi_command::EXIT:
               throw cxi_exit();
               break;
            case cxi_command::HELP:
               cxi_help();
               break;
            case cxi_command::LS:
               cxi_ls (server);
               break;
            case cxi_command::GET:
               cxi_get(server, string2[1]);
               string2.clear();
               break;
            case cxi_command::PUT:
               cxi_put(server, string2[1]);
               string2.clear();
               break;
            case cxi_command::RM:
               cxi_rm(server, string2[1]);
               string2.clear();
               break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cxi_exit& error) {
      DEBUGF ('x', "caught cxi_exit");
   }
   return 0;
}

