#!/usr/bin/perl -w

# REVERSE DCC SCRIPT for X-Chat > 2.0.4
# By David J. Oftedal <david@start.no>
# Distributed under the BSD License.
# http://www.opensource.org/licenses/bsd-license.php

# This script is intended to hijack the DCC send command in X-chat
# and reroute it to the DCC server program dccsend.
# If you're behind a firewall with all inward ports blocked,
# you can send files to people who are not behind a firewall,
# and using the mIRC DCC server feature, or the Linux
# "dccserver" program that dccsend comes with.

#Useless command here
IRC::register("Reverse DCC script", "1.0", "", "");

#Hijack all the dcc commands
IRC::add_command_handler("dcc", "dcc");

sub dcc
{

#Parse the parameters into three variables
my $line = shift @_;
my ($send, $nick, $file) = split (/ /, $line, 3);

#Get our own nick
$mynick = IRC::get_info(1);

#If the dcc command is a SEND command
if($send eq "SEND" || $send eq "send")
{

#Get the user info on the person we're sending to and grab their IP - Only X-Chat versions after 2.0.4 can do this.
my @info = IRC::user_info("$nick");
#Strip off the ident and leave only the host/ip -- We'll find the last occurrence of @ and then move one char on from that - Easy as pie.
$targetip=substr($info[1],rindex($info[1],"@")+1);

#dccsend will handle the rest. Voila. Use quotes around the file path.
IRC::print("Sending $file to $nick using the dccsend program. Make sure the user has a dccserver open on port 59.");
system("dccsend -n $mynick $targetip $file");

#Hinder X-Chat from handling the command... Not that it works
return XCHAT_EAT_XCHAT;
}

return XCHAT_EAT_NONE;
}
