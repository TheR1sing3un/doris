// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

suite("test_case_function", "query,p0") {
    sql "use test_query_db"

    def tableName1 = "test"
    def tableName2 = "baseall"

    qt_case1 """select 'number', count(*) from ${tableName2} group by
                case
                when k1=10 then 'zero'
                when k1>10 then '+'
                when k1<10 then '-' end order by 1, 2"""
    
    qt_case2 """select case when k1=0 then 'zero'
		        when k1>0 then '+'
		        when k1<0 then '-' end as wj,
		        count(*) from ${tableName1}
		        group by
                case when k1=0 then 'zero'
                     when k1>0 then '+'
                     when k1<0 then '-' end
                order by
                case when k1=0 then 'zero'
                     when k1>0 then '+'
                     when k1<0 then '-' end"""
    
    qt_case3 """
                select a.k1, case
                when b.wj is not null and b.k1>0 then 'wangjing'
                when b.wj is null then b.wj
                end as wjtest
                from (select k1, k2, case when k6='true' then 'ok' end as wj
                from ${tableName1}) as b 
                join ${tableName2} as a where a.k1=b.k1 and a.k2=b.k2 order by k1, wjtest
             """
    
    qt_case4 """select case when k1<0 then 'zhengshu' when k10='1989-03-21' then 'birthday' 
                when k2<0 then 'fu' when k7 like '%wang%' then 'wang' else 'other' end 
                as wj from ${tableName1} order by wj"""
    
    qt_case5 """select case k6 when 'true' then 1 when 'false' then -1 else 0 end 
		        as wj from ${tableName1} order by wj"""
    
    qt_case6 """select k1, case k1 when 1 then 'one' when 2 then 'two' 
		        end as wj from ${tableName1} order by k1, wj"""
    
    qt_case7 """select k1, case when k2<0 then -1 when k2=0 then 0 when k2>0 then 1 end 
		        as wj from ${tableName1} order by k1, wj"""
}
