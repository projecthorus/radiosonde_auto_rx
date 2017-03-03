#!/usr/bin/env perl
use strict;
use warnings;

my $filename = $ARGV[0];
my $fh;
if (defined $filename) {
  open($fh, "<", $filename) or die "Could not open $filename: $!";
}
else {
  $fh = *STDIN;
}

print "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n";
print "<gpx version=\"1.1\" creator=\"me\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"http://www.topografix.com/GPX/1/1\" xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\">\n";

my $line;
my $date;
my $hms;
my $lat; my $lon; my $alt;

print "<trk>\n";
print "<trkseg>\n";

while ($line = <$fh>) {
    if ($line =~ /(\d\d:\d\d:\d\d\.?\d?\d?\d?).*\ +lat:\ *(-?\d*\.\d*)\ +lon:\ *(-?\d*\.\d*)\ +alt:\ *(-?\d*\.\d*).*/) {

        $hms = $1;
        $lat = $2;
        $lon = $3;
        $alt = $4;

        $date = "";
        if ($line =~ /(\d\d\d\d-\d\d-\d\d).*/) { $date = sprintf ("%sT", $1); }
        #if ($line =~ /(\d\d\d\d)-(\d\d)-(\d\d).*/) { $date = sprintf ("%04d-%02d-%02dT", $1, $2, $3); }

        print  "  <trkpt lat=\"$lat\" lon=\"$lon\">\n";
        print  "    <ele>$alt<\/ele>\n";
        printf("    <time>%s%sZ</time>\n", $date, $hms);
        print  "  </trkpt>\n";
    }
}

print "</trkseg>\n";
print "</trk>\n";

print "</gpx>\n";

