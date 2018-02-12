#!/usr/bin/env python
#
# Converts a gem5 statistics file into a McPAT input file.
#
# Author: Daya S. Khudia
# Source: https://bitbucket.org/dskhudia/gem5tomcpat

import argparse
import cProfile
import json
import math
import types
import os
import sys
import re
from xml.etree import ElementTree as ET

# Global variables
opt_verbose = True       # Verbose output.
gem5_config = None       # gem5 config.json file.
mcpat_template = None    # Parsed McPAT XML template.
aggregated_stats = {}    # Aggregated stats.

# This is a wrapper over xml parser so that comments are preserved.
# source: http://effbot.org/zone/element-pi.htm
class PIParser(ET.XMLTreeBuilder):
   def __init__(self):
       ET.XMLTreeBuilder.__init__(self)
       # assumes ElementTree 1.2.X
       self._parser.CommentHandler = self.handle_comment
       self._parser.ProcessingInstructionHandler = self.handle_pi
       self._target.start("document", {})

   def close(self):
       self._target.end("document")
       return ET.XMLTreeBuilder.close(self)

   def handle_comment(self, data):
       self._target.start(ET.Comment, {})
       self._target.data(data)
       self._target.end(ET.Comment)

   def handle_pi(self, target, data):
       self._target.start(ET.PI, {})
       self._target.data(target + " " + data)
       self._target.end(ET.PI)

def parse(source):
    return ET.parse(source, PIParser())

def dumpMcpatOut(stats, outFile):
    rootElem = mcpat_template.getroot()
    configMatch = re.compile(r'config\.([a-zA-Z0-9_:\.]+)')
    # Replace params with values from the GEM5 config file
    for param in rootElem.iter('param'):
        name = param.attrib['name']
        value = param.attrib['value']
        if 'config' in value:
            allConfs = configMatch.findall(value)
            for conf in allConfs:
                confValue, found = getConfValue(conf)
                if found == False:
                    value = re.sub("config."+ conf + "\|", "", value)
                else:
                    if isinstance(confValue, types.ListType) and len(confValue) == 1:
                        confValue = confValue[0]
                    value = re.sub("config."+ conf + "(\|[0-9]*\.?[0-9]+)?", str(confValue), value)
            if "," in value:
                exprs = re.split(',', value)
                for i in range(len(exprs)):
                    exprs[i] = str(eval(exprs[i]))
                param.attrib['value'] = ','.join(exprs)
            else:
                param.attrib['value'] = str(eval(str(value)))

    # Replace stats with values from the GEM5 stats file
    statRe = re.compile(r'stats\.([a-zA-Z0-9_:\.]+)')
    for stat in rootElem.iter('stat'):
        name = stat.attrib['name']
        value = stat.attrib['value']
        if 'stats' in value:
            allStats = statRe.findall(value)
            expr = value
            for i in range(len(allStats)):
                if allStats[i] in stats:
                    expr = re.sub('stats.%s' % allStats[i], str(int(stats[allStats[i]])), expr)
                else:
                    # gem5 does not always print every statistic because some
                    # stats are "dependent" on others. Now, it's not clear how
                    # many of these dependent statistics exist or why they
                    # exist, but if the prereq is zero, then the stat doesn't
                    # get printed. To make sure that this script treats such
                    # stats as zero, set the expression to evaluate to 0, and
                    # don't warn about this.
                    expr = "0"
            if 'config' not in expr and 'stats' not in expr:
                stat.attrib['value'] = str(int(eval(expr)))
    # Write out the xml file
    if opt_verbose:
        print "Writing input to McPAT in: %s" % outFile
    mcpat_template.write(outFile)

def getConfValue(confStr):
    spltConf = re.split('\.', confStr)
    currConf = gem5_config
    currHierarchy = ""
    found = True
    for x in spltConf:
        currHierarchy += x
        if x not in currConf:
            if isinstance(currConf, types.ListType):
                # This is mostly for system.cpu* as system.cpu is an array.
                # This could be made better
                if x not in currConf[0]:
                    found = False
                    #print "%s does not exist in config" % currHierarchy
                else:
                    currConf = currConf[0][x]
            else:
                    found = False
                    #print "***WARNING: %s does not exist in config.***" % currHierarchy
                    #print "\t Please use the right config param in your McPAT template file"
        else:
            currConf = currConf[x]
        currHierarchy += "."
    return currConf, found

def generate_segments(myfile):
    """ Return a buffer of lines for a stats dump. """
    buf = []
    begin_flag = "Begin Simulation Statistics"
    end_flag = "End Simulation Statistics"
    for line in myfile:
        if begin_flag in line:
            continue
        elif end_flag in line:
            yield buf
            buf = []  # Clear the buffer for the next yield.
        elif line and line.strip() and not line.startswith("#"):
            buf.append(line)

def parse_segment(file_segment):
    stats = {}
    for line in file_segment:
        parts = line.split()
        statKind = parts[0]
        statValue = parts[1]
        if statValue == 'nan':
            # Don't let nan stats mess up everything else, so set it to
            # zero. But there are a lot of them, so don't issue warnings on all
            # of them.
            statValue = 0
        # We must convert to float in case we want to aggregate.
        stats[statKind] = float(statValue)
    return stats

def aggregateStats(stats):
    """ Aggregates the current set of stats into a global copy. """
    global aggregated_stats
    for stat, value in stats.iteritems():
        if "duty_cycle" in stat:
            # Duty cycles are the only stats we care about that we don't want
            # aggregated.
            continue
        if stat in aggregated_stats:
            aggregated_stats[stat] = aggregated_stats[stat] + value
        else:
            aggregated_stats[stat] = value

def readConfigFile(configFile):
    global gem5_config
    if opt_verbose:
        print "Reading config from: %s" % configFile
    F = open(configFile)
    gem5_config = json.load(F)
    F.close()

def readMcpatFile(templateFile):
    global mcpat_template
    if opt_verbose:
        print "Reading McPAT template from: %s" % templateFile
    mcpat_template = parse(templateFile)

def process(file_segment, mcpat_out_fname, out_dir, phase=None, aggregate=False):
    """ Process the stats file and generate a McPAT input XML file.

    phase should be set to None if we only want a single pass of the gem5 stats
    file. Otherwise, it should start from zero.
    """
    stats = parse_segment(file_segment)
    if phase == None or not aggregate:
        ext_idx = mcpat_out_fname.rfind(".")
        prefix = mcpat_out_fname[:ext_idx]
        ext = mcpat_out_fname[ext_idx+1:]
        if phase == None:
          new_name = "%s.%s" % (prefix, ext)
        else:
          new_name = "%s.%d.%s" % (prefix, phase, ext)
        mcpat_out_fname = os.path.join(out_dir, new_name)
        dumpMcpatOut(stats, mcpat_out_fname)
    else:
        aggregateStats(stats)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gem5_stats_file", help="gem5 statistics file.")
    parser.add_argument("gem5_config_file", help="gem5 config.json")
    parser.add_argument("mcpat_template_file", help="McPAT template file")
    parser.add_argument("-m", "--multiple_phases", action="store_true",
        help="The stats file contains multiple stats dumps for multiple program phases.")
    parser.add_argument("-a", "--aggregate", action="store_true",
        help="If we have multiple phases, aggregate all phases instead of "
        "writing multiple McPAT files.")
    parser.add_argument("-q", "--quiet",
        action="store_false", dest="verbose", default=True,
        help="don't print status messages to stdout")
    parser.add_argument("-d", "--dir",
        default=".", help="McPAT input file output directory.")
    parser.add_argument("-o", "--out",
        action="store", dest="out", default="mcpat-out.xml",
        help="output file (input to McPAT)")
    args = parser.parse_args()

    if not args.verbose:
        global opt_verbose
        opt_verbose = False

    phase = None
    if args.multiple_phases:
        phase = 0

    # These only need to be read once.
    readConfigFile(args.gem5_config_file)
    readMcpatFile(args.mcpat_template_file)

    with open(args.gem5_stats_file) as gem5_stats_file:
        for segment in generate_segments(gem5_stats_file):
            process(segment,
                    args.out,
                    args.dir,
                    phase=phase,
                    aggregate=args.aggregate)
            if args.multiple_phases:
                phase = phase + 1
            else:
                return
        if args.aggregate:
            dumpMcpatOut(aggregated_stats, os.path.join(args.dir, args.out))

if __name__ == '__main__':
    main()
