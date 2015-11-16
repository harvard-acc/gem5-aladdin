#!/usr/bin/env python
#
# Converts a gem5 statistics file into a McPAT input file.
#
# Author: Daya S. Khudia
# Source: https://bitbucket.org/dskhudia/gem5tomcpat

import argparse
import json
import math
import types
import os
import sys
import re
from xml.etree import ElementTree as ET

# Global variables
opt_verbose = True     # Verbose output.
gem5_config = None     # gem5 config.json file.
mcpat_template = None  # Parsed McPAT XML template.

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
                confValue = getConfValue(conf)
                value = re.sub("config."+ conf, str(confValue), value)
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
                    expr = re.sub('stats.%s' % allStats[i], stats[allStats[i]], expr)
                else:
                    # gem5 does not always print every statistic because some
                    # stats are "dependent" on others. Now, it's not clear how
                    # many of these dependent statistics exist or why they
                    # exist, but if the prereq is zero, then the stat doesn't
                    # get printed. To make sure that this script treats such
                    # stats as zero, set the expression to evaluate to 0, and
                    # don't warn about this.
                    expr = "0"
                    # print "***WARNING: %s does not exist in stats***" % allStats[i]
                    # print "\t Please use the right stats in your McPAT template file"

            if 'config' not in expr and 'stats' not in expr:
                stat.attrib['value'] = str(eval(expr))
    # Write out the xml file
    if opt_verbose: print "Writing input to McPAT in: %s" % outFile
    mcpat_template.write(outFile)

def getConfValue(confStr):
    spltConf = re.split('\.', confStr)
    currConf = gem5_config
    currHierarchy = ""
    for x in spltConf:
        currHierarchy += x
        if x not in currConf:
            if isinstance(currConf, types.ListType):
                # This is mostly for system.cpu* as system.cpu is an array.
                # This could be made better
                if x not in currConf[0]:
                    print "%s does not exist in config" % currHierarchy
                else:
                    currConf = currConf[0][x]
            else:
                    print "***WARNING: %s does not exist in config.***" % currHierarchy
                    print "\t Please use the right config param in your McPAT template file"
        else:
            currConf = currConf[x]
        currHierarchy += "."
    return currConf


def readStatsFile(statsFile):
    stats = {}
    if opt_verbose: print "Reading GEM5 stats from: %s" %  statsFile.name
    begin_flag = "Begin Simulation Statistics"
    end_flag = "End Simulation Statistics"
    ignores = re.compile(r'^---|^$')
    comments = re.compile("^#")
    statLine = re.compile(r'([a-zA-Z0-9_\.:-]+)\s+([-+]?[0-9]+\.[0-9]+|[-+]?[0-9]+|nan|inf)')
    count = 0
    for line in statsFile:
        # Ignore empty lines.
        if line.strip() and not comments.match(line):
            if begin_flag in line:
              continue
            if end_flag in line:
              return stats
            statKind = statLine.match(line).group(1)
            statValue = statLine.match(line).group(2)
            if statValue == 'nan':
                # Don't let nan stats mess up everything else, so set it to
                # zero. But there are a lot of them, so don't warn on all of
                # them either.
                # print "\tWarning (stats): %s is nan. Setting it to 0" % statKind
                statValue = '0'
            stats[statKind] = statValue
    return stats

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

def process(gem5_stats_file, mcpat_out_fname, out_dir, phase=None):
    """ Process the stats file and generate a McPAT input XML file.

    phase should be set to None if we only want a single pass of the gem5 stats
    file. Otherwise, it should start from zero.
    """
    stats = readStatsFile(gem5_stats_file)
    if not phase == None:
        ext_idx = mcpat_out_fname.rfind(".")
        prefix = mcpat_out_fname[:ext_idx]
        ext = mcpat_out_fname[ext_idx+1:]
        new_name = "%s.%d.%s" % (prefix, phase, ext)
        mcpat_out_fname = os.path.join(out_dir, new_name)
    dumpMcpatOut(stats, mcpat_out_fname)

def main():
    # usage = "usage: %prog [options] <gem5 stats file> <gem5 config file (json)> <mcpat template file>"
    parser = argparse.ArgumentParser()
    parser.add_argument("gem5_stats_file", help="gem5 statistics file.")
    parser.add_argument("gem5_config_file", help="gem5 config.json")
    parser.add_argument("mcpat_template_file", help="McPAT template file")
    parser.add_argument("-m", "--multiple_phases", action="store_true",
        help="The stats file contains multiple stats dumps for multiple program phases.")
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
        while True:
            try:
                process(gem5_stats_file,
                        args.out,
                        args.dir,
                        phase=phase)
                if args.multiple_phases:
                    phase = phase + 1
                else:
                    return
            except IOError as e:
                print e
                return

if __name__ == '__main__':
    main()
