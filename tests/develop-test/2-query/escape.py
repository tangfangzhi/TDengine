###################################################################
#           Copyright (c) 2021 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-

import sys
from util.log import *
from util.cases import *
from util.sql import *


class TDTestCase:
    def caseDescription(self):
        '''
        case1: [TD-12251] json type containing single quotes cannot be inserted
        case2: [TD-12334] '\' escape unknown
        case3: [TD-11071] escape table creation problem
        case5: [TD-12815] like wildcards (% _)  are not supported nchar type
        '''
        return

    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)
        self._conn = conn

    def run(self):
        print("running {}".format(__file__))
        tdSql.execute("drop database if exists escape")
        tdSql.execute("create database if not exists escape")
        tdSql.execute('use escape')

        # [TD-12251]
        tdSql.execute('create stable st (ts timestamp,t int) tags(metrics json)')
        tdSql.execute(r"insert into t1 using st tags('{\"a\":\"a\",\"b\":\"\'a\'=b\"}') values(now,1)")
        tdSql.query('select * from st')
        tdSql.checkData(0, 2, '''{"a":"a","b":"'a'=b"}''')

        # [TD-12334]
        tdSql.execute('create table car (ts timestamp, s int) tags(j int)')
        tdSql.execute(r'create table `zz\ ` using car tags(11)')
        tdSql.execute(r'create table `zz\\ ` using car tags(11)')
        tdSql.execute(r'create table `zz\\\ ` using car tags(11)')
        tdSql.query(r'select tbname from car where tbname like "zz\\\\ "')
        tdSql.checkRows(1)
        tdSql.checkData(0, 0, r"zz\\ ")

        tdSql.query(r'show tables like "zz\\\\ "')
        tdSql.checkRows(1)
        tdSql.checkData(0, 0, r"zz\\ ")

        tdSql.query(r'show tables like "zz\\ "')
        tdSql.checkRows(1)

        # [TD-11071]
        tdSql.execute('create table es (ts timestamp, s int) tags(j int)')
        tdSql.execute(r'create table `zz\t` using es tags(11)')
        tdSql.execute(r'create table `zz\\n` using es tags(11)')
        tdSql.execute(r'create table `zz\r\ ` using es tags(11)')
        tdSql.execute(r'create table `      ` using es tags(11)')
        tdSql.query(r'select tbname from es')
        tdSql.checkData(0, 0, r'zz\t')
        tdSql.checkData(1, 0, r'zz\\n')
        tdSql.checkData(2, 0, r'zz\r\ ')
        tdSql.checkData(3, 0, r'      ')

        # [TD-6232]
        tdSql.execute('create table tt(ts timestamp, i nchar(128))')
        tdSql.execute(r"insert into tt values(1591060628000, '\t')")
        tdSql.execute(r"insert into tt values(1591060638000, '\n')")
        tdSql.execute(r"insert into tt values(1591060648000, '\r')")
        tdSql.execute(r"insert into tt values(1591060658000, '\\')")
        tdSql.execute(r"insert into tt values(1591060668000, '\"')")
        tdSql.execute(r"insert into tt values(1591060678000, '\'')")
        tdSql.execute(r"insert into tt values(1591060688000, '\%')")
        tdSql.execute(r"insert into tt values(1591060698000, '\_')")
        tdSql.execute(r"insert into tt values(1591060708000, '\9')")

        tdSql.query(r"select * from tt where i='\t'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\n'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\r'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\\'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\"'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\''")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\%'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\_'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='\9'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i='9'")
        tdSql.checkRows(1)

        # [TD-12815] like wildcard(%, _) are not supported nchar
        tdSql.execute(r"insert into tt values(1591070708000, 'h%d')")
        tdSql.execute(r"insert into tt values(1591080708000, 'h_j')")
        tdSql.query(r"select * from tt where i like 'h\%d'")
        tdSql.checkRows(1)
        tdSql.query(r"select * from tt where i like 'h\_j'")
        tdSql.checkRows(1)
    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
