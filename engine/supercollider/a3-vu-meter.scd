// A³ Core is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// A³ Core is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with A³ Core.  If not, see <https://www.gnu.org/licenses/>.

// © Copyright 2021 Raphael Eismann

s.options.device_("Supercollider");

s.options.numInputBusChannels  = 16;
s.options.numOutputBusChannels = 2;

s.options.memSize = 128;
s.options.numBuffers = 1;

s.boot;

~mic = NetAddr.new("192.168.43.55", 7771);    // create the NetAddr
//~vid = NetAddr.new("192.168.43.100", 7771);    // create the NetAddr
~replyrate = 25;
~peaklag_in = 2;
~peaklag_out = 3;
~peaklag_vid = 0;
~num_vu_meter = 16;
~num_input_vu = 4;
~num_output_vu = 8;

s.waitForBoot({

//syntax: SendPeakRMS.ar(sig, replyRate: 20.0, peakLag: 3, cmdName: '/reply', replyID: -1)

~num_input_vu.do{
		arg item;
		{SendPeakRMS.ar(In.ar(2 + item, 1), ~replyrate, ~peaklag_in, "/reply" ++ item)}.play;
		OSCFunc({ |msg| ~mic.sendMsg("/vu/" ++ item, msg[3], msg[4]);}, "/reply" ++ item);
	};

~num_output_vu.do{
		arg item;
		var after_in_vu, channel;
		channel = 1 + item;
		after_in_vu = ~num_input_vu + item;
		{SendPeakRMS.ar(In.ar(2 + after_in_vu, 1), ~replyrate, ~peaklag_out, "/reply" ++ after_in_vu)}.play;
		OSCFunc({ |msg| ~mic.sendMsg("/vu/" ++ after_in_vu, msg[3], msg[4]);}, "/reply" ++ after_in_vu);
//		OSCFunc({ |msg| ~vid.sendMsg("/vu_out/rms/" ++ channel, msg[3]);}, "/reply" ++ after_in_vu);
//		OSCFunc({ |msg| ~vid.sendMsg("/vu_out/peak/" ++ channel, msg[4]);}, "/reply" ++ after_in_vu);
	};

~num_input_vu.do{
		arg item;
		var after_out_vu, channel;
		channel = 1 + item;
		after_out_vu = ~num_input_vu + ~num_output_vu + item;
		{SendPeakRMS.ar(In.ar(2 + after_out_vu, 1), ~replyrate, ~peaklag_vid, "/reply" ++ after_out_vu)}.play;
//		OSCFunc({ |msg| ~vid.sendMsg("/vu_in/rms/" ++ channel, msg[3]);}, "/reply" ++ after_out_vu);
//		OSCFunc({ |msg| ~vid.sendMsg("/vu_in/peak/" ++ channel, msg[4]);}, "/reply" ++ after_out_vu);
	};
});
