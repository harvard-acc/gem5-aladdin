# Copyright (c) 2007 The Regents of The University of Michigan
# Copyright (c) 2010 The Hewlett-Packard Development Company
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Nathan Binkert

import m5
import os

from m5 import internal
from m5.internal.stats import schedStatEvent as schedEvent
from m5.objects import Root
from m5.util import attrdict, fatal

# Try and include SQLAlchemy. If this is successful we allow it to be used,
# otherwise it is disabled.
try:
    from sqlalchemy import *
    from sqlalchemy.orm import sessionmaker
    import sql as m5sql
    SQL_ENABLED = True
except:
    print "Failed to import sqlalchemy or m5.stats.sql. SQL will not be enabled."
    SQL_ENABLED = False

# Global variable to determine if statistics output is enabled
STATS_OUTPUT_ENABLED = False

# Keep track of the number of stats dumps performed. Lets us keep
# track of the data we write to the SQL database
dump_count = 0

class OutputSQL(object):
  """ Class which outputs the stats to a database. """
    def __init__(self, filename):
      """ Create the database and add the tables used to store the stats. """
        self.filename = filename
        self.db = m5sql.create_database(self.filename)
        Session = sessionmaker(bind = self.db)
        m5sql.create_tables(self.db)
        self.session = Session()

    def visit(self, stat):
      """ Write the stats to the database.

      On the first dump we also write the information about the stats to the
      database. This is only done once.
      """
        global dump_count

        if dump_count == 0:
            m5sql.add_stat_info(stat, self.session)

        m5sql.store_stat_value(stat, self.session, dump_count)

    def __call__(self, context):
        global dump_count

        # On the first dump write the information about the stats to the
        # database.
        if dump_count == 0:
            for name,stat in context.iterate():
                m5sql.add_stat_info(stat, self.session)

        # Write the values to the database.
        for name,stat in context.iterate():
            m5sql.store_stat_value(stat, self.session, dump_count)

        # Commit our changes to the database. All changes are commited once
        # to allow SQLAlchemy to optimize the database accesses, resulting in
        # faster stats dumps.
        self.session.commit()

    def valid(self):
      """ Checks if the database file exists at the specified location. """
      return os.path.exists(self.filename)

    def begin(self, desc):
        m5sql.store_dump_desc(self.session, desc, dump_count)

    def end(self):
      """ Commits all the data at once. """
      self.session.commit()

outputList = []
def initText(filename, desc=True):
    output = internal.stats.initText(filename, desc)
    outputList.append(output)
    global STATS_OUTPUT_ENABLED
    STATS_OUTPUT_ENABLED = True

def initSimStats():
    internal.stats.initSimStats()

def init_SQL(outputDirectory, filename):
    """ Add the stats database as an output and add it to outputList.

    Args:
      outputDirectory: The directlry to store the database.
      filename: The filename to which the stats are written.
    """
    global SQL_ENABLED

    # Take the supplied filename and prepend the output directory.
    import os
    filename = os.path.join(outputDirectory, filename)

    if SQL_ENABLED:
        output = OutputSQL(filename)
        outputList.append(output)
        global STATS_OUTPUT_ENABLED
        STATS_OUTPUT_ENABLED = True
        return True
    else:
        return False

def stats_output_enabled():
    """ Check that at least one statistics output format is enabled.

    Return:
      True if at least one output format is enabled, False otherwise
    """
    return STATS_OUTPUT_ENABLED


names = []
stats_dict = {}
stats_list = []
raw_stats_list = []
def enable():
    '''Enable the statistics package.  Before the statistics package is
    enabled, all statistics must be created and initialized and once
    the package is enabled, no more statistics can be created.'''
    __dynamic_cast = []
    for k, v in internal.stats.__dict__.iteritems():
        if k.startswith('dynamic_'):
            __dynamic_cast.append(v)

    for stat in internal.stats.statsList():
        for cast in __dynamic_cast:
            val = cast(stat)
            if val is not None:
                stats_list.append(val)
                raw_stats_list.append(val)
                break
        else:
            fatal("unknown stat type %s", stat)

    for stat in stats_list:
        if not stat.check() or not stat.baseCheck():
            fatal("stat check failed for '%s' %d\n", stat.name, stat.id)

        if not (stat.flags & flags.display):
            stat.name = "__Stat%06d" % stat.id

    def less(stat1, stat2):
        v1 = stat1.name.split('.')
        v2 = stat2.name.split('.')
        return v1 < v2

    stats_list.sort(less)
    for stat in stats_list:
        stats_dict[stat.name] = stat
        stat.enable()

    internal.stats.enable();

def prepare():
    '''Prepare all stats for data access.  This must be done before
    dumping and serialization.'''

    for stat in stats_list:
        stat.prepare()

lastDump = 0
def dump(stats_desc=""):
    '''Dump all statistics data to the registered outputs'''
    if not STATS_OUTPUT_ENABLED:
        return

    curTick = m5.curTick()

    global lastDump
    assert lastDump <= curTick
    if lastDump == curTick:
        return
    lastDump = curTick

    internal.stats.processDumpQueue()

    prepare()

    for output in outputList:
        if output.valid():
            output.begin(stats_desc)
            for stat in stats_list:
                output.visit(stat)
            output.end()

    global dump_count
    dump_count = dump_count + 1

def reset():
    '''Reset all statistics to the base state'''

    # call reset stats on all SimObjects
    root = Root.getInstance()
    if root:
        for obj in root.descendants(): obj.resetStats()

    # call any other registered stats reset callbacks
    for stat in stats_list:
        stat.reset()

    internal.stats.processResetQueue()

flags = attrdict({
    'none'    : 0x0000,
    'init'    : 0x0001,
    'display' : 0x0002,
    'total'   : 0x0010,
    'pdf'     : 0x0020,
    'cdf'     : 0x0040,
    'dist'    : 0x0080,
    'nozero'  : 0x0100,
    'nonan'   : 0x0200,
})
