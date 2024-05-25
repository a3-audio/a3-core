s.options.device_("BlackHole 16ch");

s.options.numInputBusChannels  = 16;
s.options.numOutputBusChannels = 2;

s.options.memSize = 65536;
s.options.numBuffers = 4096;

s.boot;

//~core = NetAddr.new("192.168.43.50", 9000);    // create the NetAddr
~mic = NetAddr.new("192.168.43.51", 7771);    // create the NetAddr
~vid = NetAddr.new("192.168.43.103", 7771);    // create the NetAddr
~replyrate = 20;
~peaklag_in = 2;
~peaklag_out = 5;
~peaklag_vid = 3;
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
		OSCFunc({ |msg| ~vid.sendMsg("/vu_out/rms/" ++ channel, msg[3]);}, "/reply" ++ after_in_vu);
		OSCFunc({ |msg| ~vid.sendMsg("/vu_out/peak/" ++ channel, msg[4]);}, "/reply" ++ after_in_vu);
	};

~num_input_vu.do{
		arg item;
		var after_out_vu, channel;
		channel = 1 + item;
		after_out_vu = ~num_input_vu + ~num_output_vu + item;
		{SendPeakRMS.ar(In.ar(2 + after_out_vu, 1), ~replyrate, ~peaklag_vid, "/reply" ++ after_out_vu)}.play;
		OSCFunc({ |msg| ~vid.sendMsg("/vu_in/rms/" ++ channel, msg[3]);}, "/reply" ++ after_out_vu);
		OSCFunc({ |msg| ~vid.sendMsg("/vu_in/peak/" ++ channel, msg[4]);}, "/reply" ++ after_out_vu);
	};
});
