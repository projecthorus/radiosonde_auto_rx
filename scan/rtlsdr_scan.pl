#!/usr/bin/env perl

use strict;
use warnings;

use POSIX qw(strftime);


## rs_detect.c return:
## #define  DFM   2
## #define  RS41  3
## #define  RS92  4
## #define  M10   5
## #define  iMet  6

my $log_dir = "log";

my $utc = strftime('%Y%m%d_%H%M%S', gmtime);
print $utc, "UTC", "\n";

my $powfile = sprintf "log_power\.csv";


my $ppm = 54;
my $fos = 403200000 * ( 1 + $ppm/1000000 );  # 403.2MHz-Linie

my $scan_f1 = "400.6M";
my $scan_f2 = "405.9M";

my $line;
my @fields;

my $date;
my $time;
my $f1;
my $f2;
my $f;
my $step;
my @db;

my $i;
my $j;

my $peak = 0;
my $peak_start;
my $peak_end = 0;

my $freq;
my $ret;
my $inv;
my $dec;
my $wavfile;
my $filter;
my $breite = "";
my $rs;
my $WFM;

my $squelch;
my $snr = 6;

my @peakarray;
my $num_peaks;

print "[rtl_power: scan $scan_f1:$scan_f2]\n";
system("timeout 30 rtl_power -p $ppm -f $scan_f1:$scan_f2:800 -i20 -1 $powfile 2>/dev/null");
if ( $? == -1 ) {
    print "Fehler: $!\n";
}


my $fh;
open ($fh, '<', "$powfile") or die "Kann '$powfile' nicht oeffnen: $!\n";


@peakarray = ();
my $num_lines = 0;

while ($line = <$fh>) {
    $num_lines += 1;
    chomp $line;
    @db = split(",", $line);
    $date = $db[0];
    shift @db;
    $time = $db[0];
    shift @db;
    $f1 = $db[0];
    shift @db;
    $f2 = $db[0];
    shift @db;
    $step = $db[0];
    shift @db;
    shift @db;

    my $sum = eval join '+', @db;
    my $mean = $sum / scalar(@db);
    #printf "level: %.1f\n", $mean;
    $squelch = $mean + $snr;

    $peak = 0;
    $peak_start = $peak_end = 0;

    for ($j = 0; $j < scalar(@db)-1; $j++) {

        if ($db[$j] > $squelch) {
            if ($peak == 0) {
                $peak_start = $f1 + $j*$step;
            }
            $peak = 1;
        }
        elsif ($peak > 0) {
            if ($db[$j+1] <= $squelch) {
                $peak_end = $f1 + ($j-1)*$step;
                $peak = 0;
            }
            if ($peak_start < $peak_end) {
                $freq = $peak_start + ($peak_end-$peak_start)/2;
                if  ( !($freq > $fos-1000 && $freq < $fos+1000) ) { ## 403.2MHz-Linie auslassen
                    push @peakarray, $freq;
                }
            }
        }
            
    }
}
close $fh;

if ($num_lines == 0) {
    print "[reset dvb-t ...]\n";
    reset_dvbt();
}
else {
    $num_peaks = scalar(@peakarray);
    for ($j = 0; $j < $num_peaks-1; $j++) {    
        if ($peakarray[$j+1]-$peakarray[$j] < 10e3) {  # DFM peak-to-peak: 6kHz
            push @peakarray, $peakarray[$j]+($peakarray[$j+1]-$peakarray[$j])/2;
        }
        elsif ($peakarray[$j+1]-$peakarray[$j] < 34e3) {  # iMet peak-to-peak: 24kHz
            push @peakarray, $peakarray[$j]+($peakarray[$j+1]-$peakarray[$j])/2;
        }
    }

    print "[rtl_fm: detect/decode]\n";

    for ($j = 0; $j < $num_peaks; $j++) {

        $freq = sprintf "%.0f", $peakarray[$j];

        eval {
            local $SIG{ALRM} = sub {die "alarm\n"};
            alarm 30; # beide Bandbreiten
            print "\n$freq Hz: ";  # detect ohne highpass 20 
            system("timeout 12s rtl_fm -p $ppm -M fm -s 15k -f $freq 2>/dev/null |\ 
                    sox -t raw -r 15k -e s -b 16 -c 1 - -r 48000 -t wav - 2>/dev/null |\ 
                    ./rs_detect -s -z -t 8 2>/dev/null");
            $ret = $? >> 8;
            if (!$ret) {  # mehr Bandbreite bei iMet
                system("timeout 12s rtl_fm -p $ppm -M fm -s 36k -o 4 -f $freq 2>/dev/null |\ 
                        sox -t raw -r 36k -e s -b 16 -c 1 - -r 48000 -t wav - 2>/dev/null |\ 
                        ./rs_detect -s -z -t 8 2>/dev/null");
                $ret = $? >> 8;
            }
            alarm 0;
        };
        if ($@) {
            die unless  $@ =~ /alarm/;
            reset_dvbt();
            print "[reset dvb-t ...]\n";
        }

        if ($ret) {
            if ($ret & 0x80) {
                $inv = '-i';
                $ret = - (0x100 - $ret);  # obwohl ($ret & 0x80) = core dump
            }
            else { $inv = ''; }
            $rs = "";
            $WFM = "";
            if (abs($ret) == 2) { $rs = "dfm";  $breite = "15k"; $dec = './dfm06 -vv --ecc'; $filter = "lowpass 2000 highpass 20"; }
            if (abs($ret) == 3) { $rs = "rs41"; $breite = "12k"; $dec = './rs41ecc --ecc -v'; $filter = "lowpass 2600"; }
            if (abs($ret) == 4) { $rs = "rs92"; $breite = "12k"; $dec = './rs92gps --vel2 -a almanac.txt'; $filter = "lowpass 2500 highpass 20"; }
            if (abs($ret) == 5) { $rs = "m10";  $breite = "24k"; $dec = './m10x -vv'; $filter = "highpass 20"; }
            if (abs($ret) == 6) { $rs = "imet"; $breite = "40k"; $dec = './imet1ab -v'; $filter = "highpass 20";  $WFM = "-o 4"; }
            if ($inv) { print "-";} print uc($rs)," ($utc)\n";
            $utc = strftime('%Y%m%d_%H%M%S', gmtime);
            $wavfile = $rs."-".$utc."Z-".$freq."Hz.wav";
            if ($rs) {
                system("timeout 30s rtl_fm -p $ppm -M fm $WFM -s $breite -f $freq 2>/dev/null |\ 
                        sox -t raw -r $breite -e s -b 16 -c 1 - -r 48000 -b 8 -t wav - $filter 2>/dev/null |\ 
                        tee $log_dir/$wavfile | $dec $inv 2>/dev/null");
            }
        }

    }
    print "\n";
}

print "\n=\n";


## $snr: peak sensitivity: wenn zu niedrig, viele Kandidaten;
##       wenn zu hoch, zu wenige (rs41 sendet nur 1/2 sek.
## andere Methoden:
## Fenster/Bandbreite aufsummieren/integrieren; Schwerpunkt;
## rtl-sdr IQ-Band: 1 Minute aufnehmen und dann analysieren/scannen.
##
## rs92: aktuellen (SEM) almanac.txt
##       wenn brdc-emphemerides (-e) benutzt werden, darauf achten, dass diese
##       aktuell sind, wenn ueber laengeren Zeitraum gescannt wird
##
## rs_detect.c und decoder: gleiche Polaritaet in read_bits_fsk(),
##                          am besten als gemeinsame include-Datei wav_audio.c
###################################################################################################


## http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line
## usbreset.c -> reset_usb
sub reset_dvbt {
    my @devices = split("\n",`lsusb`);
    foreach my $line (@devices) {
        if ($line =~ /\w+\s(\d+)\s\w+\s(\d+):\sID\s([0-9a-f]+):([0-9a-f]+).+Realtek Semiconductor Corp\./) {
            if ($4 eq "2832"  ||  $4 eq "2838") {
                system("./reset_usb /dev/bus/usb/$1/$2");
            }
        }
    }
}

