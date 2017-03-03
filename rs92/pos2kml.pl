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

print "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
print "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n";

my $line;
my $hms;
my $lat; my $lon; my $alt;

print "  <Document>\n";
print "    <Placemark>\n";
print "      <LineString>\n";
print "      <altitudeMode>absolute</altitudeMode>\n";
print "        <coordinates>\n";
print "           ";
while ($line = <$fh>) {
    if ($line =~ /(\d\d:\d\d:\d\d).*\ +lat:\ *(-?\d*\.\d*)\ +lon:\ *(-?\d*\.\d*)\ +alt:\ *(-?\d*\.\d*).*/) {
        $hms = $1;
        $lat = $2;
        $lon = $3;
        $alt = $4;
        print  " $lon,$lat,$alt";
    }
}
print "\n";
print "        </coordinates>\n";
print "      </LineString>\n";
print "    </Placemark>\n";
print "  </Document>\n";

print "</kml>\n";

