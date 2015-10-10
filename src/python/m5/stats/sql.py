# Copyright (c) 2012 ARM Limited
# All rights reserved
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
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
# Authors: Sascha Bischoff
#

try:
    from sqlalchemy import *
    from sqlalchemy.orm import sessionmaker
    from sqlalchemy.ext.declarative import declarative_base
except:
    print "Unable to import sqlalchemy!"
    raise

import os

from m5.util import fatal, panic
from m5.internal.stats import \
    ScalarInfo, VectorInfo, Vector2dInfo, FormulaInfo, DistInfo, \
    Deviation, Dist, Hist

Base = declarative_base()

def create_database(filename):
    """ Create the database used to store the stats. If it exists, delete it.

    Args:
      filename: The filename usd to store the stats.

    Return:
      A handle to the database.
    """

    if os.path.exists(filename):
        os.remove(filename)

    try:
        db = create_engine('sqlite:///' + filename)
    except:
        panic("Failed to open database %s!", filename)
    db.echo = False
    return db


def create_tables(db):
    """Create the tables used to store the stats and information about them.

    Args:
      db: The database in which to create the tables.
    """
    metadata = MetaData(db)

    # Stores the information about the stats
    stats_table = Table('stats', metadata,
                  Column('id', Integer, primary_key = True),
                  Column('name', String),
                  Column('desc', String),
                  Column('subnames', String),
                  Column('y_subnames', String),
                  Column('subdescs', String),
                  Column('precision', Integer),
                  Column('prereq', Integer),
                  Column('flags', Integer),
                  Column('x', Integer),
                  Column('y', Integer),
                  Column('type', String),
                  Column('formula', String),
    )

    # Stores scalar values
    scalar_value_table = Table('scalarValue', metadata,
                         Column('id', Integer),
                         Column('dump', Integer),
                         Column('value', Float),
    )

    # Stores vectors, 2d vectors and is also used to store formulas as they can
    # be scalars or vectors based on the stats used in the calculation.
    vector_value_table = Table('vectorValue', metadata,
                         Column('id', Integer),
                         Column('dump', Integer),
                         Column('value', Binary),
    )

    # Stores distributions.
    dist_value_table = Table('distValue', metadata,
                       Column('id', Integer),
                       Column('dump', Integer),
                       Column('sum', Float),
                       Column('squares', Float),
                       Column('samples', Float),
                       Column('min', Float),
                       Column('max', Float),
                       Column('bucket', Float),
                       Column('vector', Binary),
                       Column('min_val', Float),
                       Column('max_val', Float),
                       Column('underflow', Float),
                       Column('overflow', Float),
    )

    # Attaches descriptions to each dump.
    dump_desc_table = Table("dumpDesc", metadata,
                       Column('id', Integer),
                       Column('desc', String),
    )

    metadata.create_all()


class StatsInfoClass(Base):
    """ Class used to insert the information about stats into the database. """

    __tablename__ = 'stats'

    id = Column(Integer, primary_key = True)
    name = Column(String)
    desc = Column(String)
    subnames = Column(String)
    y_subnames = Column(String)
    subdescs = Column(String)
    precision = Column(Integer)
    prereq = Column(Integer)
    flags = Column(Integer)
    x = Column(Integer)
    y = Column(Integer)
    type = Column(String)
    formula = Column(String)

    def __init__(self, stat):
        self.id = stat.id
        self.name = stat.name
        self.desc = stat.desc
        self.flags = stat.flags
        self.precision = stat.precision
        if stat.prereq:
            self.prereq = stat.prereq.id
        self.type = str(type(stat))

        if isinstance(stat, VectorInfo):
            self.subnames = ','.join(stat.subnames)
            self.subdescs = ','.join(stat.subdescs)
        elif isinstance(stat, FormulaInfo):
            self.subnames = ','.join(stat.subnames)
            self.subdescs = ','.join(stat.subdescs)
            self.formula = stat.formula
        elif isinstance(stat, Vector2dInfo):
            self.subnames = ','.join(stat.subnames)
            self.y_subnames = ','.join(stat.y_subnames)
            self.subdescs = ','.join(stat.subdescs)
            self.x = stat.x
            self.y = stat.y
        elif isinstance(stat, DistInfo):
            data = stat.data

            if data.type ==  Deviation:
                the_type = "Deviation"
            elif data.type == Dist:
                the_type = "Distribution"
            elif data.type == Hist:
                the_type = "Histogram"

            self.type = the_type

    def __repr__(self):
        return "<User('%d','%s','%s','%s','%s','%s','%d','%d','%d','%d','%d', \
            '%s','%s')>" % (self.id, self.name, self.desc, self.subnames,
            self.y_subnames, self.subdescs, self.precision, self.prereq,
            self.flags, self.x, self.y, self.type, self.formula)


class ScalarValueClass(Base):
    """ Class used to insert scalar stats into the database. """

    __tablename__ = 'scalarValue'

    id = Column(Integer, primary_key = True)
    dump = Column(Integer)
    value = Column(Integer)

    def __init__(self, id, dump, value):
        self.id = id
        self.dump = dump
        self.value = value

    def __repr__(self):
        return "<User('%d','%d','%f')>" % (self.id, self.dump, self.value)

class VectorValueClass(Base):
    """ Class used to insert vector stats into the database. """

    __tablename__ = 'vectorValue'

    id = Column(Integer, primary_key = True)
    dump = Column(Integer)
    value = Column(Binary)

    def __init__(self, id, dump, value):
        import array
        self.id = id
        self.dump = dump
        a = array.array('f', value)
        self.value = a.tostring()

    def __repr__(self):
        return "<User('%d','%d','%s')>" % (self.id, self.dump, self.value)


class DistValueClass(Base):
    """ Class used to insert dictribution stats into the database. """

    __tablename__ = 'distValue'

    id = Column(Integer, primary_key = True)
    dump = Column(Integer)
    sum = Column(Float)
    squares = Column(Float)
    samples = Column(Float)
    min = Column(Float)
    max = Column(Float)
    bucket = Column(Float)
    vector = Column(Binary)
    min_val = Column(Float)
    max_val = Column(Float)
    underflow = Column(Float)
    overflow = Column(Float)

    def __init__(self, id, dump):
        self.id = id
        self.dump = dump


    def __repr__(self):
        return "<User('%d','%d','%f','%f','%f','%f','%f','%f','%s','%f','%f', \
            '%f','%f')>" % (self.id, self.dump, self.sum, self.squares,
            self.samples, self.min, self.max, self.bucket, self.vector,
            self.min_val, self.max_val, self.underflow, self.overflow)

class DumpDescValueClass(Base):
    """ Class that stores the description of a stats dump. """
    __tablename__ = 'dumpDesc'

    id = Column(Integer, primary_key = True)
    desc = Column(String)

    def __init__(self, id, desc):
        self.id = id
        self.desc = desc

    def __repr__(self):
        return "<User('%d','%s')>" % (self.id, self.desc)

def add_stat_info(stat, session):
    """ Add the information about a stat.

    Args:
      name: The name of the stat.
      stat: The stat itself.
      session: The session associated with the database.
    """
    temp = StatsInfoClass(stat)
    session.add(temp)

def store_stat_value(stat, session, dumpCount):
    """ Stores the value of a stat.

    Args:
       stat: The stat itself.
       session: The session associated with the database.
       dumpCount: The number of dumps that have occured. Used to store multiple
         stats dumps in one database.
    """
    if isinstance(stat, ScalarInfo):
        temp = ScalarValueClass(id = stat.id, dump = dumpCount,
                                value = stat.value())
        session.add(temp)

    elif isinstance(stat, VectorInfo) or isinstance(stat, FormulaInfo):
        temp = VectorValueClass(id = stat.id, dump = dumpCount,
                                value = stat.value())
        session.add(temp)

    elif isinstance(stat, Vector2dInfo):
        temp = VectorValueClass(id = stat.id, dump = dumpCount,
                                value = stat.cvec)
        session.add(temp)

    elif isinstance(stat, DistInfo):
        import array

        data = stat.data

        temp = DistValueClass(id = stat.id, dump = dumpCount)

        temp.sum = data.sum
        temp.squares = data.squares
        temp.samples = data.samples

        if data.type == Dist or data.type == Hist:
            temp.min = data.min
            temp.max = data.max
            temp.bucket = data.bucket_size
            a = array.array('f', data.cvec)
            temp.vector = a.tostring()

        if data.type == Dist:
            temp.min_val = data.min_val
            temp.max_val = data.max_val
            temp.underflow = data.underflow
            temp.overflow = data.overflow

        session.add(temp)

    else:
        panic("Unable to output stat %s. Unsupported stat type!", stat)

def store_dump_desc(session, desc, dump_count):
    """ Stores the description of this dump. """
    temp = DumpDescValueClass(id=dump_count, desc=desc)
    session.add(temp)
