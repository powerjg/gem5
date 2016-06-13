
import subprocess
import math

def mean(data):
    """Return the sample arithmetic mean of data."""
    n = len(data)
    if n < 1:
        raise ValueError('mean requires at least one data point')
    return sum(data)/float(n)

def _ss(data):
    """Return the std square error of data"""
    c = mean(data)
    ss = sum((x-c)**2 for x in data)
    return ss

def std(data):
    """Calculates the population standard deviation."""
    n = len(data)
    ss = _ss(data)
    pvar = ss/n # the population variance
    return pvar**0.5

def sem(cvar, n):
    """ Calculates the std error of the mean or SEM
        This depends on the coefficient of variation
        and number of samples
       @param cvar coefficient of variation
       @param n  number of observations
    """
    return cvar / (n**0.5)

def getVariance(outdir, n, stat='commit.fp_insts'):
    """Calculates the coeff of variation in a stat
       across multiple simulations. The value of the
       stat is added across multiple CPUs.

       Returns the co-efficient of variation i.e. the
       sample std. dev normalized to the mean.

       @param outdir  root directory from where the
                   simulations are subdirectories
                   assumes they are named 0, 1, 2 etc.
       @param n  number of subdirectories
       @param stat  part of the string for the stat
    """

    statfiles = ["%s/%d/stats.txt" % (outdir,i) for i in xrange(n)]

    data = []
    for sf in statfiles:
      # count the stat
      awkcmd = "grep %s %s  | awk 'BEGIN{sum=0} {sum+=$2} END{print sum}'"\
                    % (stat, sf)
      #print "DEBUG Stats: ", awkcmd
      try:
        output = subprocess.check_output(awkcmd, shell=True)
        dv =  float( output )
        #print dv
        if dv != .0:
          data.append(dv)
      except:
        print "DEBUG Stats: failed ", stat, " read for ", sf, " out = ", out

    if len(data) == 0:
      return 0
    return std(data) / mean(data)

def std(data):
    """Calculates the population standard deviation."""
    n = len(data)
    ss = _ss(data)
    pvar = ss/n # the population variance
    return pvar**0.5

if __name__=='__main__':

    import sys
    outdir = sys.argv[1]

    print outdir
    print getVariance(outdir, 10)


