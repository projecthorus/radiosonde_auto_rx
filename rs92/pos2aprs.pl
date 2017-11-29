#!/usr/bin/env perl
## aprs-output provided by daniestevez
use strict;
use warnings;


my $filename = undef;
my $date = undef;

my $mycallsign;
my $passcode;
my $comment;

while (@ARGV) {
  $mycallsign = shift @ARGV;
  $passcode = shift @ARGV;
  $comment = shift @ARGV;
  $filename = shift @ARGV;
}

my $fpi;

if (defined $filename) {
  open($fpi, "<", $filename) or die "Could not open $filename: $!";
}
else {
  $fpi = *STDIN;
}

my $fpo = *STDOUT;


my $line;

my $hms;
my $lat; my $lon; my $alt;
my $sign;
my $NS; my $EW;
my $str;

my $speed = 0.00;
my $course = 0.00;

my $callsign;

my $temp;

print $fpo "user $mycallsign pass $passcode vers \"RS decoder\"\n";

while ($line = <$fpi>) {

    print STDERR $line; ## entweder: alle Zeilen ausgeben

    if ($line =~ /(\d\d):(\d\d):(\d\d\.?\d?\d?\d?).*\ +lat:\ *(-?\d*)(\.\d*)\ +lon:\ *(-?\d*)(\.\d*)\ +alt:\ *(-?\d*\.\d*).*/) {

    #print STDERR $line; ## oder: nur Zeile mit Koordinaten ausgeben

        $hms = $1*10000+$2*100+$3;

        if ($4 < 0) { $NS="S"; $sign *= -1; }
        else        { $NS="N"; $sign = 1}
        $lat = $sign*$4*100+$5*60;

        if ($6 < 0) { $EW="W"; $sign = -1; }
        else        { $EW="E"; $sign = 1; }
        $lon = $sign*$6*100+$7*60;

        $alt = $8*3.28084; ## m -> feet

        if ($line =~ /(\d\d\d\d)-(\d\d)-(\d\d).*/) {
            $date = $3*10000+$2*100+($1%100);
        }

        if ($line =~ /vH:\ *(\d+\.\d+)\ +D:\ *(\d+\.\d+).*/) {
            $speed = $1*3.6/1.852;  ## m/s -> knots
            $course = $2;
        }

	    if ($line =~ /\(([\w]+)\)/) {
	        $callsign = $1;
	    }

	    if ($line =~ /T=(-?[\d.]+)C/) {
	         $temp = " T=$1C";
	    }
	    else {
	         $temp = "";
	    }

	    $str = sprintf("$mycallsign>APRS,TCPIP*:;%-9s*%06dh%07.2f$NS/%08.2f${EW}O%03d/%03d/A=%06d$comment$temp", $callsign, $hms, $lat, $lon, $course, $speed, $alt);
	    print $fpo "$str\n";

    }
    #elsif ($line =~ / # xdata = (.*)/) { ## nicht, wenn (oben) alle Zeilen ausgeben werden
    #    if ($1) {
    #        print STDERR $line;
    #    }
    #}
}

close $fpi;
close $fpo;

