#!/usr/bin/python

import logging, os, re, shlex, shutil, signal, subprocess, tempfile
from datetime import datetime, timedelta
from junit_xml import TestCase, TestCasesFile, TestSuite
from stat import *
from string import Template

def mycopytree(src, dest):
    names = os.listdir(src)
    errors = []
    myLog = logging.getLogger()

    if os.path.exists(dest):
        if not os.path.isdir(dest):
            myLog.error("Destination directory ('%s') exists but is NOT a directory" % (dest))
            return
    else:
        os.makedirs(dest)

    for name in names:
        srcname = os.path.join(src, name)
        destname = os.path.join(dest, name)
        try:
            if os.path.islink(srcname):
                linkto = os.readlink(srcname)
                os.symlink(linkto, destname)
            elif os.path.isdir(srcname):
                if name <> ".git": # Skip the .git directory
                    shutil.copytree(srcname, destname, symlinks=True)
            else:
                shutil.copy2(srcname, destname)
        except OSError as why:
            errors.append((srcname, destname, str(why)))
        except shutil.Error as err:
            errors.extend(err[0])
    # End of for over names
    try:
        shutil.copystat(src, dest)
    except shutil.WindowsError:
        pass
    except OSError as why:
        errors.append((src, dest, str(why)))
    # Check if any errors
    if len(errors):
        myLog.error("Error copying '%s' to '%s', specific errors follow:" % (src, dest))
        for err in errors:
            myLog.error("\tFrom '%s' to '%s': %s" % (err[0], err[1], str(err[2])))

class Dependence(object):
    """Convenience class to describe a dependence between
       two jobs
    """
    UNINIT  = 0x0
    UNKNOWN = 0x1
    SUCCESS = 0x2
    FAIL    = 0x3

    def __init__(self, jobName, slot, isSkippable):
        """Initializes a dependence. The 'slot'
           parameter is the dependence slot on the
           destination job and the 'job' parameter is
           the source job (what the dependence is waiting
           on). isSkippable is T if the dependence can be
           ignored if it fails
        """
        # Public variables
        self.jobName = jobName
        self.job = None
        self.slot = slot

        # Private variables
        self._status = Dependence.UNINIT
        self._isSkippable = isSkippable

    def setJob(self, job):
        assert(job.name == self.jobName)
        self.job = job
        self._status = Dependence.UNKNOWN

    def setStatus(self, status):
        self._status = status

    def getStatus(self):
        return self._status

    def isUninitialized(self):
        """Returns True if the dependence hasn't yet been
           fully initialized
        """
        return self._status == Dependence.UNINIT

    def isUnknown(self):
        """Returns True if the dependence is still being
           evaluated
        """
        return self._status == Dependence.UNKNOWN

    def isFail(self):
        """Returns True if the dependence failed
        """
        return self._status == Dependence.FAIL

    def isSkippable(self):
        """Returns True if the dependence can be ignored
        """
        return self._isSkippable

class JobTypeObject(object):
    """Describes a job type to be run. A job type
       describes a set of jobs
    """
    def __init__(self, inputDict):
        """Creates a new job type. The parameters are:
               - inputDict is a dictionary with 'name', 'keywords', 'isLocal',
                 'run-cmd', 'param-cmd', 'sandbox', 'req-repos' and optionally 'timeout',
                 'prologue-cmd', 'epilogue-cmd', and 'env-vars'
        """
        # Public variables (to avoid getter/setter)
        self.name = inputDict['name']
        self.keywords = inputDict['keywords']
        self.depends = inputDict.get('depends', ())
        self.isLocal = inputDict['isLocal']
        self.run_cmd = inputDict['run-cmd']
        self.param_cmd = inputDict['param-cmd']
        self.prologue_cmd = inputDict.get('prologue-cmd', "")
        self.epilogue_cmd = inputDict.get('epilogue-cmd', "")
        self.sandbox = inputDict['sandbox']
        self.req_repos = inputDict['req-repos']
        self.timeout = inputDict.get('timeout', JobObject.defaultTimeout) # Defaults to defaultTimeout
        self.env_vars = inputDict.get('env-vars', {}).copy() # Defaults to empty dir


        self._myLog = logging.getLogger()

        # Check the sandbox keywords
        cleanSandbox = []
        for val in self.sandbox:
            if val in ('single', 'shareOK', 'writeEnv', 'local', 'shared', 'emptyShared', 'copyShared',
                       'emptyLocal', 'copyLocal'):
                cleanSandbox.append(val)
            else:
                self._myLog.warning("%s specifies invalid sandbox parameter '%s'... ignoring" % (str(self), val))
        if len(cleanSandbox) <> len(self.sandbox):
            self.sandbox = tuple(cleanSandbox)

    def __repr__(self):
        # TODO: Put more detail
        return self.__str__()

    def __str__(self):
        return "<JobType '%s'>" % (self.name)


class JobObject(object):
    """Describes a job to be run. Each job will contain
        - The attributes required to run the job
        - A set of dependences that must be met
    """
    UNCONFIGURED_JOB = 0x0 # JobObject is not configured yet
    WAITING_JOB      = 0x1 # JobObject is waiting for the status of some of its dependences
    READY_JOB        = 0x10 # JobObject is ready to run (all dependence satisfied and cleared)
    BLOCKED_JOB      = 0x11 # JobObject will never be ready because a dependence failed
    RUNNING_LOCAL    = 0x20 # JobObject is runnning locally
    RUNNING_REMOTE   = 0x21 # JobObject is running over a job manager
    DONE_OK          = 0x40 # JobObject completed and returned a status of 0
    DONE_FAIL_LAUNCH = 0x41 # JobObject failed to launch
    DONE_FAIL_RUN    = 0x42 # JobObject failed to complete
    DONE_FAIL_STATUS = 0x43 # JobObject ran and completed with a non-zero status

    @classmethod
    def setGlobals(cls, **globals):
        for k, v in globals.iteritems():
            setattr(cls, k, v)

    def __init__(self, inputDict, jobType, dependenceCount):
        """Create a new job. The parameters are:
            - inputDict is a dictionary with 'name', 'run-args',
              'param-args', 'depends' and optionally 'sandbox', 'timeout',
              and 'env-vars'
            - jobType is an object of type JobTypeObject
            - dependenceCount is the number of dependences
        """

        # Public variables (to avoid getter/setter)
        self.jobid = -1     # Will be set when the execute method is called
        self.name = inputDict['name']
        self.run_args = inputDict['run-args']
        self.param_args = inputDict.get('param-args', "")
        self.timeout = inputDict.get('timeout', None)
        self.env_vars = jobType.env_vars.copy() # We take what jobType has and override things
        self.env_vars.update(inputDict.get('env-vars', {}))
        self.req_repos = inputDict.get('req-repos', ()) + jobType.req_repos
        self.jobType = jobType

        # These three variables are used when the process starts running
        self._outFile = None    # Out file to be used when this job executes
        self._errFile = None    # Err file to be used when this job executes
        self._startTime = None  # Time the process started running
        self._endTime   = None  # Time the process ended running

        # Private variables

        # First the rest of the configuration variables
        self._sandbox = inputDict.get('sandbox', ())

        # The logger (for now use the same global one)
        self._myLog = logging.getLogger()

        # Dependence stuff
        self._waiters = dict() # Key is name, value is pair of job and slot
        self._dependence = []
        self._signalerToProp = dict() # Key is the name of the signaling job and value is
                                      # tuple (position, isSkippable)
        for inDep, count in zip(inputDict['depends'], range(0, len(inputDict['depends']))):
            self._dependence.append(Dependence(inDep[0], count, inDep[1]))

        self._depth = 0

        self._sharedRootDirectory = None  # Name of the root directory for this job
                                          # that is shared with LUSTRE
        self._privateRootDirectory = None # Name of the root directory for this job that is
                                          # private to the machine

        # Place where we put the result
        self._testSuite = TestSuite(self.name, package=self.name)

        # Status variables
        if dependenceCount == 0:
            self._jobStatus = JobObject.READY_JOB
            # Do not add to ready job since the depth may not be accurate
        else:
            self._jobStatus = JobObject.UNCONFIGURED_JOB

        self._recomputeStatus = False # True if status needs to be updated
        self._recomputeDirs = True    # True if _dirs needs to be updated
        self._dirs = {'private': None, 'shared': None,
                      'copy-private': None, 'copy-shared': None,} # Cached directory information

        # Variables used to determine how to build the directories
        self._shareOK = False
        self._wholeMachine = False
        # For the next two variables: -2 (no directory), -1 (clean/original directory), any
        # other number, the dependence from which we inherit the directory
        self._requireSharedRoot = -2  # Do not require a shared root
        self._requirePrivateRoot = -2 # Do not require a private root
        # For the next two variables OR value of : 0 (default), 1 (empty), 2 (same directory), 4 (copy)
        self._modeSharedRoot = 0
        self._modePrivateRoot = 0
        self._startInShared = False
        self._newDirectory = False # Will be true if a new directory needs to be created
        self._envProducer = False  # Will be true if this job produces an environment

        # Very simple propagation of values
        if self._sandbox is None:
            self._sandbox = jobType.sandbox
        else:
            self._sandbox = jobType.sandbox + self._sandbox
        if self.timeout is None or self.timeout == 0:
            self.timeout = jobType.timeout

        # Parse the sandbox information
        self._parseSandbox()

        self._myLog.debug("Created a JobObject for %s" % (str(self)))

    def addWaiter(self, waiterJob, slot):
        """Adds a job to the waiters list. That waiting
           job will be notified when this job completes
           (either successfully or not).
           Returns False is the waiter was NOT added
           and True otherwise
        """
        assert(waiterJob.name not in self._waiters),\
        "%r already present in dependency list of %r "%(self,waiterJob)
        # Check if we are already all done
        if self._recomputeStatus:
            self._updateStatus()
        assert(self._recomputeStatus == False)
        if self._jobStatus & 0x40 or self._jobStatus == JobObject.BLOCKED_JOB:
            # We already have run the job or are blocked
            # don't add the waiter
            return False
        self._waiters[waiterJob.name] = (waiterJob, slot)
        self._myLog.debug("Added %s as a waiter to %s; now have %d waiters" %
                         (str(waiterJob), str(self), len(self._waiters)))
        if self._depth < waiterJob._depth + 1:
            self._depth = waiterJob._depth + 1
            self._propagateDepthInfo()
        self._myLog.debug("%s now has depth %d" % (str(self), self._depth))
        return True

    def extendDependenceList(self, job, isSkippable):
        """Adds additional dependence slots"""
        if self._recomputeStatus:
            self._updateStatus()
        if self._jobStatus == JobObject.UNCONFIGURED_JOB or self._jobStatus == JobObject.WAITING_JOB:
            slotId = len(self._dependence)
            self._dependence.append(Dependence(job.name, slotId, isSkippable))
            self._dependence[slotId].setJob(job)
            self._recomputeStatus = True
            return slotId
        else:
            self._myLog.error("Cannot extend the dependence list of %s because its status is not UNCONFIGURED or WAITING" % (str(self)))
            assert(False)

    def addDependence(self, signalingJob):
        """Adds a job to the list of dependence.
           Returns the slot used for that dependence
        """
        depCount = 0
        for dep in self._dependence:
            if dep.jobName == signalingJob.name:
                dep.setJob(signalingJob)
                break
            depCount += 1
        assert(depCount < len(self._dependence))
        self._myLog.debug("%s depends on %s at slot %d" % (
            str(self), str(signalingJob), depCount))
        self._recomputeStatus = True # Status will need to be recomputed
        return depCount

    def satisfyDependence(self, signalingJob, slot):
        """Notifies this job that a dependence is satisfied"""
        if slot < 0:
            # Special case of satisfying a skippable slot. We have to find the proper slot
            self._myLog.debug("Noting skippable dependence for %s (skipping %s" % (str(self), signalingJob))
            for dep in self._dependence:
                if dep.jobName == signalingJob:
                    assert(self._dependence[slot].isSkippable())
                    self._dependence[slot].setStatus(Dependence.FAIL)
        else:
            self._myLog.debug("Satisfying dependence slot %d of %s with %s" % (slot, str(self), str(signalingJob)))
            assert(self._dependence[slot].job is signalingJob)

            self._dependence[slot].setStatus((Dependence.SUCCESS if signalingJob.getStatus() == JobObject.DONE_OK else Dependence.FAIL))
        self._recomputeStatus = True
        self._updateStatus()
        if self._jobStatus == JobObject.READY_JOB:
            # We can move ourself to the ready queue
            self._myLog.debug("Satisfaction triggered %s" % (str(self)))
        elif self._jobStatus == JobObject.BLOCKED_JOB:
            self._myLog.warning("%s is BLOCKED due to failure in %s" % (str(self), str(signalingJob)))

            # We note a failure test case and record the "path" it took us to get here
            testCase = TestCase("_main_" + self.name, self.name, 0, '', '')
            testCase.add_skipped_info("JobObject was not run due to a dependence failure", '')
            self._testSuite.add_test_case(testCase)
            for dep in self._dependence:
                self._testSuite.merge_cases(dep.job._testSuite)

            # Inform the jobs dependending on us
            for v in self._waiters.itervalues():
                v[0].satisfyDependence(self, v[1])

    def updateDepth(self, depth):
        """Updates the depth to be the maximum of 'depth'
           and 'self.depth'
        """
        if depth > self._depth:
            self._depth = depth
            self._propagateDepthInfo()

    def getDepth(self):
        """Returns the depth"""
        return self._depth

    def getStatus(self):
        """Returns the status of this job (ready, un-runnable, etc"""
        if self._recomputeStatus:
            self._updateStatus()
        assert(not self._recomputeStatus)
        return self._jobStatus

    def getIsWholeMachine(self):
        """Returns true if this job requires the entire local machine"""
        return self._wholeMachine

    def getIsEnvProducer(self):
        """Returns true if this job produces the env dir"""
        return self._envProducer

    def getIsTerminalJob(self):
        """Returns true if this is a "terminal" job (ie: no other job
           is dependent on it"""
        return (len(self._waiters) == 0)

    def getTestSuite(self):
        return self._testSuite

    def getDirectories(self):
        """Returns a dictionary containing 'private', 'shared', 'copy-private' and 'copy-shared'
           indicating the directories to use for the job and the directories to
           copy. If 'None', the directory is not required. If "", a new directory
           needs to be created (name determined by the caller). This call is only
           accurate after the job is ready to run (all dependences met).
           A job can request two types of directories:
               - a "shared" directory that is on a LUSTRE share (shared across nodes)
               - a "private" directory located just on the node where the job is run
           For each of these directories, the job can specify:
               - whether it wants to inherit the directory from one of its parent (and which one)
                 or get a copy of the original downloaded repository
               - whether it wants a new directory or if it is OK using the parent's directory
                 in place (for the original directory, a copy is always provided)
               - whether it is OK sharing the directory with other concurrent jobs
        """
        if not self._recomputeDirs:
            return self._dirs

        self._dirs['private'] = self._dirs['shared'] = self._dirs['copy-private'] = self._dirs['copy-shared'] = None

        # We get our status because some jobs may start running even if they
        # have skipped us as a dependence
        # If that's the case, we return no directories
        status = self.getStatus()
        # READY_JOB (job wants to start running) and DONE_OK (job is parent of other job)
        if status is not JobObject.DONE_OK and status is not JobObject.READY_JOB:
            self._recomputeDirs = False
            return self._dirs

        inheritFrom = max(self._requirePrivateRoot, self._requireSharedRoot)
        if inheritFrom >= 0:
            # Check to see if the one we inherit from is a failure (skippable)
            if self._dependence[inheritFrom].job.getStatus() is not JobObject.DONE_OK:
                # We look for another one
                inheritFrom += 1
                while inheritFrom < len(self._dependence) and \
                      self._dependence[inheritFrom].job.getStatus() is not JobObject.DONE_OK:
                    inheritFrom += 1
            # It's possible we don't find anything
            if inheritFrom >= len(self._dependence):
                inheritFrom = None
            else:
                dirs = self._dependence[inheritFrom].job.getDirectories()
                inheritFrom = (dirs['private'], dirs['shared'])
        else:
            inheritFrom = None

        if self._requirePrivateRoot > -2:
            if self._modePrivateRoot == 1:
                self._dirs['private'] = ""
            elif self._modePrivateRoot == 2:
                if inheritFrom is None:
                    self._myLog.warning("Changing from 'same directory' to 'empty' for private directory of %s (no valid ancestors)"
                                        % (str(self)))
                else:
                    self._dirs['private'] = inheritFrom[0]
                    # Check if it doesn't exist and if so, use the shared copy
                    # Need to copy to deal with base path issues
                    self._dirs['copy-private'] = self._checkForNoneDir(self._dirs['private'],
                                                                       inheritFrom[1], ("private", "shared"))
                    if self._dirs['private'] is None:
                        self._dirs['private'] = ""
                    else:
                        self._dirs['copy-private'] = None
            elif self._modePrivateRoot == 4:
                self._dirs['private'] = ""
                if inheritFrom is None:
                    self._dirs['copy-private'] = JobObject.cleanDirectories
                else:
                    self._dirs['copy-private'] = inheritFrom[0]
                    self._dirs['copy-private'] = self._checkForNoneDir(self._dirs['copy-private'],
                                                                       inheritFrom[1],
                                                                       ("private", "shared"))
            else:
                assert(False);

        if self._requireSharedRoot > -2:
            if self._modeSharedRoot == 1:
                self._dirs['shared'] = ""
            elif self._modeSharedRoot == 2:
                if inheritFrom is None:
                    self._myLog.warning("Changing from 'same directory' to 'empty' for shared directory of %s (no valid ancestors)"
                                        % (str(self)))
                    self._dirs['shared'] = ""
                else:
                    self._dirs['shared'] = inheritFrom[1]
                    # Check if it doesn't exist and if so, use the shared copy
                    # Need to copy to deal with base path issues
                    self._dirs['copy-shared'] = self._checkForNoneDir(self._dirs['shared'],
                                                                      inheritFrom[0], ("shared", "private"))
                    if self._dirs['shared'] is None:
                        self._dirs['shared'] = ""
                    else:
                        self._dirs['copy-shared'] = None
            elif self._modeSharedRoot == 4:
                self._dirs['shared'] = ""
                if inheritFrom is None:
                    self._dirs['copy-shared'] = JobObject.cleanDirectories
                else:
                    self._dirs['copy-shared'] = inheritFrom[1]
                    self._dirs['copy-shared'] = self._checkForNoneDir(self._dirs['copy-shared'],
                                                                      inheritFrom[0],
                                                                      ("shared", "private"))
            else:
                assert(False);

        self._recomputeDirs = False
        self._myLog.debug("%s's initial directories are %s" % (str(self), str(self._dirs)))
        return self._dirs

    def isLocal(self):
        """Returns true if the job runs on the local machine"""
        self._myLog.error("%s needs to define its own isLocal!!" % (str(self)))
        assert(False)

    def execute(self, jobId):
        """Tries to execute the job. Will return
           the status of the job. If the job was
           successfully launched, it will return RUNNING_LOCAL or
           RUNNING_REMOTE
        """
        self.jobid = jobId

    def poll(self):
        """Checks if the job finished executing.
           This will return True if the job is done
           and False if it is still running
        """
        self._myLog.error("%s needs to define its own poll!!" % (str(self)))
        assert(False)


    def signalJobDone(self, returnCode, releaseDirs, errString = None):
        """Called when the job finished executing. Updates everything
           and triggers any dependence
        """
        self._recomputeStatus = True
        if self._outFile is not None:
            self._outFile.seek(0) # Go back to the beginning of the files
        if self._errFile is not None:
            self._errFile.seek(0)
        testCases = [None, None]
        if returnCode == 0:
            # We check whether we have an output file to use for the result
            cmdLine = Template(self.jobType.param_cmd + " output " + self.param_args)
            myEnv = self._getEnvironment()
            cmdLine = cmdLine.substitute(myEnv)
            args = shlex.split(cmdLine)
            self._myLog.debug("%s getting output file with command '%s'" % (str(self), str(args)))
            outputFile = None
            myEnv['PYTHONIOENCODING'] = 'utf-8'
            try:
                outputFile = subprocess.check_output(args, cwd=myEnv['JJOB_START_HOME'], env=myEnv, shell=False)
            except subprocess.CalledProcessError:
                self._myLog.warning("Could not get output file for %s... ignoring" % (str(self)))

            outputFile = outputFile.strip()
            if len(outputFile) == 0:
                outputFile = None

            if outputFile is not None:
                self._myLog.debug("%s got output file '%s'" % (str(self), outputFile))
            else:
                self._myLog.debug("%s has no defined output file" % (str(self)))

            if outputFile is not None and (not os.path.isfile(outputFile) or not os.path.isabs(outputFile)):
                self._myLog.warning("Output file '%s' for %s is not a valid file or not an absolute path... ignoring result" %
                                    (outputFile, str(self)))
                outputFile = None

            if outputFile:
                testCases[1] = TestCasesFile(outputFile)

            testCases[0] = TestCase("_main_" + self.name, self.name,
                                    (self._endTime - self._startTime).total_seconds(),
                                    self._outFile.read(-1).decode('utf-8'), self._errFile.read(-1).decode('utf-8'))
            self._jobStatus = JobObject.DONE_OK
        else:
            testCases[0] = TestCase("_main_" + self.name, self.name,
                                    (self._endTime - self._startTime).total_seconds() if self._endTime is not None else 0,
                                    self._outFile.read(-1).decode('utf-8') if self._outFile is not None else '', '')
            if errString is not None:
                # Internal launch error
                if returnCode == -1:
                    # Job launched but did not complete
                    testCases[0].add_error_info("JobObject launched but did not run to completion: %s" % (errString),
                                                self._errFile.read(-1).decode('utf-8'))
                    self._jobStatus = JobObject.DONE_FAIL_RUN
                elif returnCode == -2:
                    # This means the job did not start running
                    testCases[0].add_error_info("JobObject failed to launch: %s" % (errString))
                    self._jobStatus = JobObject.DONE_FAIL_LAUNCH
                elif returnCode == -3:
                    # This means the job had a configuration error
                    testCases[0].add_error_info("JobObject had a configuration error: %s" % (errString))
                    self._jobStatus = JobObject.DONE_FAIL_LAUNCH
                else:
                    assert(False) # No other internal error codes
            else:
                testCases[0].add_failure_info("JobObject failed with return code: %d" % (returnCode),
                                              self._errFile.read(-1).decode('utf-8') if self._errFile is not None else '')
                self._jobStatus = JobObject.DONE_FAIL_STATUS
        # Done if returnCode
        if self._outFile is not None:
            self._outFile.close() # This should automatically delete the file
        if self._errFile is not None:
            self._errFile.close()

        self._outFile = self._errFile = None

        # Update the test suite
        self._testSuite.add_test_case(testCases[0])
        if testCases[1] is not None:
            self._testSuite.add_test_case(testCases[1])
        # Merge this test suite with the ones in the history
        if returnCode <> -3:
            for dep in self._dependence:
                self._testSuite.merge_cases(dep.job._testSuite)

        # Notify all waiters
        for v in self._waiters.itervalues():
            v[0].satisfyDependence(self, v[1])

        # Release hold on directories
        if releaseDirs:
            self._releaseJobDirectories(returnCode <> 0)

    def _parseSandbox(self):
        inheritDirFrom = None
        for criteria in self._sandbox:
            if criteria == "single":
                self._wholeMachine = True
            elif criteria == "nosingle":
                self._wholeMachine = False
            elif criteria == "shareOK":
                self._shareOK = True
            elif criteria == "noshareOK":
                self._shareOK = False
            elif criteria == "writeEnv":
                self._envProducer = True
            elif criteria == "local":
                self._requirePrivateRoot = -1
            elif criteria == "nolocal":
                self._requirePrivateRoot = -2
                self._startInShared = True # We have no more local, so we have to start in shared
            elif criteria == "shared":
                self._requireSharedRoot = -1
                if self._requirePrivateRoot == -2:
                    self._startInShared = True
            elif criteria == "noshared":
                self._requireSharedRoot = -2
                self._startInShared = False # No share, so start in private for sure
            elif criteria.startswith('inherit'):
                if inheritDirFrom is not None:
                    self._myLog.error("%s specifies multiple inherit parameters in sandbox" % (str(self)))
                    assert(False)
                try:
                    inheritDirFrom = int(criteria[7:])
                except ValueError:
                    inheritDirFrom = len(self._dependence) # Will trigger error

                if inheritDirFrom >= len(self._dependence):
                    self._myLog.error("%s specifies an invalid inherit target (got %s, should be between 0 and %d)" %
                                (str(self), criteria[7:], len(self._dependence) - 1))
                    assert(False)
            elif criteria == "copyShared":
                self._modeSharedRoot |= 4
            elif criteria == "nocopyShared":
                self._modeSharedRoot &= ~4
            elif criteria == "emptyShared":
                self._modeSharedRoot |= 1
            elif criteria == "noemptyShared":
                self._modeSharedRoot &= ~1
            elif criteria == "copyLocal":
                self._modePrivateRoot |= 4
            elif criteria == "nocopyLocal":
                self._modePrivateRoot &= ~4
            elif criteria == "emptyLocal":
                self._modePrivateRoot |= 1
            elif criteria == "noemptyLocal":
                self._modePrivateRoot &= ~1
            else:
                self._myLog.warning("%s specifies unknown criteria in sandbox ('%s') ... ignoring." % (str(self), criteria))
        # end for over self.sandbox
        if self._requirePrivateRoot == -2 and self._requireSharedRoot == -2:
            self._myLog.warning("%s specifies no working directory ... defaulting to a private directory." % (str(self)))
            self._requirePrivateRoot = -1
        if inheritDirFrom is not None:
            if self._requirePrivateRoot == -1:
                self._requirePrivateRoot = inheritDirFrom
            if self._requireSharedRoot == -1:
                self._requireSharedRoot = inheritDirFrom
        if self._newDirectory and self._shareOK:
            self._myLog.warning("%s specifies a new directory and that sharing is OK ... ignoring sharing." % (str(self)))
            self._shareOK = False
        if self._shareOK and self._requirePrivateRoot < 0 and self._requireSharedRoot < 0:
            self._myLog.warning("%s specifies sharing is OK but uses the original directories... ignoring sharing." % (str(self)))
            self._shareOK = False
        if self._wholeMachine and (not self.jobType.isLocal):
            self._myLog.warning("%s is a remote job and specifies wholeMachine... ignoring wholeMachine as it only applies to local jobs." % (str(self)))
            self._wholeMachine = False

        if self._modePrivateRoot <> 0 and self._modePrivateRoot <> 1 and \
           self._modePrivateRoot <> 2 and self._modePrivateRoot <> 4:
            self._myLog.warning("%s: Invalid mode for the local root... using defaults" % (self));
            self._modePrivateRoot = 0

        if self._modeSharedRoot <> 0 and self._modeSharedRoot <> 1 and \
           self._modeSharedRoot <> 2 and self._modeSharedRoot <> 4:
            self._myLog.warning("%s: Invalid mode for the shared root... using defaults" % (self));
            self._modeSharedRoot = 0

        if self._modePrivateRoot == 0:
            if self._requirePrivateRoot == -1:
                self._modePrivateRoot = 4
            elif self._requirePrivateRoot >= 0:
                self._modePrivateRoot = 2
        elif self._modePrivateRoot == 2 and self._requirePrivateRoot == -1:
            self._myLog.warning("Forcing copy of local root since no inherit")
            self._modePrivateRoot = 4

        if self._modeSharedRoot == 0:
            if self._requireSharedRoot  == -1:
                self._modeSharedRoot = 4
            elif self._requireSharedRoot >= 0:
                self._modeSharedRoot = 2
        elif self._modeSharedRoot == 2 and self._requireSharedRoot == -1:
            self._myLog.warning("Forcing copy of shared root since no inherit")
            self._modeSharedRoot = 4


    def _propagateDepthInfo(self):
        """Helper function to update the depth of parents
        """
        for dep in self._dependence:
            if not dep.isUninitialized():
                # For other dependences we don't care since
                # they have already fired away
                dep.job.updateDepth(self._depth + 1)

    def _updateStatus(self):
        if not (self._jobStatus & 0xF0):
            self._jobStatus = None
            for dep in self._dependence:
                # Note that we only break out if UNCONFIGURED_JOB because,
                # in the other cases, we want to only return the other
                # status if the job is fully configured. In other words,
                # if there is one unitialized dependence, this will return
                # UNCONFIGURED_JOB. BLOCKED_JOB also overrides WAITING_JOB
                if dep.isUninitialized():
                    self._jobStatus = JobObject.UNCONFIGURED_JOB
                    break
                if dep.isUnknown():
                    if self._jobStatus is None:
                        # Don't set if we have BLOCKED_JOB (only other
                        # possibility)
                        self._jobStatus = JobObject.WAITING_JOB
                elif dep.isFail() and not dep.isSkippable():
                    # At this point, it means that a dependence failed
                    self._jobStatus = JobObject.BLOCKED_JOB
            if self._jobStatus is None:
                self._jobStatus = JobObject.READY_JOB # Nothing happened so we are all good
        #end if
        self._recomputeStatus = False

    def _checkForNoneDir(self, directory, otherOption, errorNames):
        if directory is None:
            self._myLog.warning("%s needs a %s directory from its parent; not found so using a %s directory." %
                                (str(self), errorNames[0], errorNames[1]))
            directory = otherOption
            if directory is None:
                self._myLog.error("%s could not find a proper %s directory to use" % (str(self), errorNames[0]))
            return directory
        return directory

    def _createJobDirectories(self):
        """Sets up an environment for the job to
        run in. This mostly sets up the directories
        the way the job wants them"""
        self._myLog.debug("Creating directories for %s" % (str(self)))
        if self._dirs['private'] == "":
            # We need to create a new directory for the private copy
            self._dirs['private'] = tempfile.mkdtemp(prefix="testDir_", dir=JobObject.privateRoot)
        if self._dirs['shared'] == "":
            self._dirs['shared'] = tempfile.mkdtemp(prefix="testDir_", dir=JobObject.sharedRoot)

        self._myLog.debug("Directories for %s will be '%s' (non LUSTRE) and '%s' (LUSTRE)" %
                          (str(self), self._dirs['private'], self._dirs['shared']))

    def _copyJobDirectories(self):
        """This copies any data into the job directories if required"""
        self._myLog.debug("Starting directory copies...")
        t = datetime.now()
        if self._dirs['copy-private'] is not None:
            # We need to copy this to self._dirs['private']
            assert(self._dirs['private'] is not None)
            assert(self._dirs['private'] <> "")
            if type(self._dirs['copy-private']) == type(dict()):
                self._myLog.debug("Copying multiple directories for non-LUSTRE directory")
                for k, v in self._dirs['copy-private'].iteritems():
                    destDir = self._dirs['private'] + '/' + k.lower()
                    self._myLog.debug("Copying '%s' to '%s' for the non-LUSTRE directory" %
                                      (v, destDir))
                    mycopytree(v, destDir)
            else:
                self._myLog.debug("Copying '%s' to '%s' for the non-LUSTRE directory" %
                                  (self._dirs['copy-private'], self._dirs['private']))
                mycopytree(self._dirs['copy-private'], self._dirs['private'])
        if self._dirs['copy-shared'] is not None:
            # We need to copy this to self._dirs['shared']
            assert(self._dirs['shared'] is not None)
            assert(self._dirs['shared'] <> "")
            if type(self._dirs['copy-shared']) == type(dict()):
                self._myLog.debug("Copying multiple directories for LUSTRE directory")
                for k, v in self._dirs['copy-shared'].iteritems():
                    destDir = self._dirs['shared'] + '/' + k.lower()
                    self._myLog.debug("Copying '%s' to '%s' for the LUSTRE directory" %
                                      (v, destDir))
                    mycopytree(v, destDir)
            else:
                self._myLog.debug("Copying '%s' to '%s' for the LUSTRE directory" %
                                  (self._dirs['copy-shared'], self._dirs['shared']))
                mycopytree(self._dirs['copy-shared'], self._dirs['shared'])
        t1 = datetime.now()
        self._myLog.debug("... All copies done (took %d seconds)" % (t1 - t).seconds)

    def _grabJobDirectories(self):
        """Grabs a kind of lock on the directories so that
           the shareOK properties are respected. Returns True if
           the directories could be grabbed and false otherwise
        """
        # Start with the private directory
        if self._dirs['private'] is not None:
            assert(self._dirs['private'] <> "")
            usedInfo = JobObject.allUsedPaths.get(self._dirs['private'], None)
            if (usedInfo is not None) and (usedInfo[0] or (usedInfo[1] > 0 and (not self._shareOK))):
                # We cannot run the job because of a path conflict
                self._myLog.warning("%s not running due to share conflict on non-LUSTRE '%s'" %
                                    (str(self), self._dirs['private']))
                return False
            elif usedInfo is None:
                # We create a new path information
                JobObject.allUsedPaths[self._dirs['private']] = ((not self._shareOK), 1, 0)
                self._myLog.debug("%s sole user of non-LUSTRE '%s'" % (str(self), self._dirs['private']))
                # We update the path information
            else:
                # We update the information
                JobObject.allUsedPaths[self._dirs['private']] = ((not self._shareOK) or usedInfo[0], usedInfo[1] + 1, usedInfo[2])
                self._myLog.debug("%s sharing non-LUSTRE '%s' with %d other jobs" %
                                  (str(self), self._dirs['private'], usedInfo[1]))
        # Now check the shared directory
        if self._dirs['shared'] is not None:
            assert(self._dirs['shared'] <> "")
            usedInfo = JobObject.allUsedPaths.get(self._dirs['shared'], None)
            if (usedInfo is not None) and (usedInfo[0] or (usedInfo[1] > 0 and (not self._shareOK))):
                # We cannot run the job because of a path conflict
                self._myLog.warning("%s not running due to share conflict on LUSTRE '%s'" % (str(self), self._dirs['shared']))
                # We make sure to update the allUsedPaths for jobDirs[0]
                if self._dirs['private'] is not None:
                    usedInfo = JobObject.allUsedPaths[self._dirs['private']]
                    # If we grabbed it, it was because everyone was OK sharing
                    JobObject.allUsedPaths[self._dirs['private']] = (False, usedInfo[1] - 1, usedInfo[2])
                return False
            elif usedInfo is None:
                # We create a new path information
                JobObject.allUsedPaths[self._dirs['shared']] = ((not self._shareOK), 1, 0)
                self._myLog.debug("%s sole user of LUSTRE '%s'" % (str(self), self._dirs['shared']))
            else:
                # We update the information
                JobObject.allUsedPaths[self._dirs['shared']] = ((not self._shareOK) or usedInfo[0], usedInfo[1] + 1, usedInfo[2])
                self._myLog.debug("%s sharing LUSTRE '%s' with %d other jobs" %
                                  (str(self), self._dirs['shared'], usedInfo[1]))
        return True

    def _releaseJobDirectories(self, isFail):
        """Release the hold on the directories"""
        # Start with the private directory
        failureCount = 0
        if isFail:
            failureCount = 1
        if self._dirs['private'] is not None:
            usedInfo = JobObject.allUsedPaths[self._dirs['private']]
            self._myLog.debug("%s removing hold on non-LUSTRE '%s'; now have %d users and %d failures" %
                              (str(self), self._dirs['private'], usedInfo[1] - 1, usedInfo[2] + failureCount))
            JobObject.allUsedPaths[self._dirs['private']] = (False, usedInfo[1] - 1, usedInfo[2] + failureCount)
        # Now the shared directory
        if self._dirs['shared'] is not None:
            usedInfo = JobObject.allUsedPaths[self._dirs['shared']]
            self._myLog.debug("%s removing hold on LUSTRE '%s'; now have %d users and %d failures" %
                              (str(self), self._dirs['shared'], usedInfo[1] - 1, usedInfo[2] + failureCount))
            JobObject.allUsedPaths[self._dirs['shared']] = (False, usedInfo[1] - 1, usedInfo[2] + failureCount)
        # All done

    def _getEnvironment(self, ignorePbs = False):
        env = os.environ.copy()
        localEnv = self.env_vars.copy()

        # JJ* variables override anything set in env_vars
        localEnv['JJOB_NAME'] = self.name
        localEnv['JJOB_PRIVATE_HOME'] = self._dirs['private'] if self._dirs['private'] is not None else ""
        localEnv['JJOB_SHARED_HOME'] = self._dirs['shared'] if self._dirs['shared'] is not None else ""
        localEnv['JJOB_START_HOME'] = localEnv['JJOB_SHARED_HOME'] if self._startInShared else localEnv['JJOB_PRIVATE_HOME']
        localEnv['JJOB_ENVDIR'] = JobObject.envDirectory
        localEnv['JJOB_ID'] = str(self.jobid)
        for k, v in JobObject.cleanDirectories.iteritems():
            localEnv['JJOB_INITDIR_' + k] = v
        id = 0
        for dep in self._dependence:
            parentDirs = dep.job.getDirectories()
            dir = parentDirs['private']
            if dir is not None:
                assert(dir <> "")
                localEnv['JJOB_PARENT_PRIVATE_HOME_' + str(id)] = dir
            dir = parentDirs['shared']
            if dir is not None:
                assert(dir <> "")
                localEnv['JJOB_PARENT_SHARED_HOME_' + str(id)] = dir
            id = id + 1

        # Substitute environments in definitions of env_vars
        # This allows (in particular), the users to have things
        # like ${JJOB_NAME} in env-vars
        # We do at most 5 levels of nesting
        for i in range(0, 5):
            substLocalEnv = {}
            doBreak = True
            for k, v in localEnv.iteritems():
                initV = v
                t = Template(v)
                # Do in this order to make sure localEnv has precedence
                v = t.safe_substitute(localEnv)
                t = Template(v);
                v = t.safe_substitute(env)
                if initV is not v:
                    doBreak = False
                substLocalEnv[k] = v
            localEnv = substLocalEnv
            if doBreak:
                break
        # End while

        self._myLog.debug("Local environment additions/overrides for %s are: %s" % (str(self), str(substLocalEnv)))

        # We override anything that is set in the environment
        env.update(substLocalEnv)
        if ignorePbs:
            # We remove anything that starts with PBS_
            keysToRemove = []
            for k, v in env.iteritems():
                if k.startswith('PBS_'):
                    keysToRemove.append(k)
            for k in keysToRemove:
                del env[k]
        return env

    def __repr__(self):
        """TODO: Need to make it more detailed"""
        return str(self)

    def __str__(self):
        return "<JobObject '%s'>" % (self.name)


class LocalJobObject(JobObject):
    """A job that executes on the local machine"""

    def isLocal(self):
        return True

    def execute(self, jobId):
        """Executes the job locally"""
        super(LocalJobObject, self).execute(jobId)
        if self._recomputeStatus:
            self._updateStatus()
        if self._recomputeDirs:
            self.getDirectories()

        if self._jobStatus == JobObject.READY_JOB:

            self._myLog.info("Launching local job %s of type %s" % (str(self), str(self.jobType)))
            # First we create the environment for the job
            self._createJobDirectories()

            # We now check if the directories are OK to run in
            if not self._grabJobDirectories():
                # We don't actually need to destroy any directories since
                # the only reason we can't grab the directories is
                # we share them (ie: none were created in createJobDirectories)
                return JobObject.READY_JOB
            # At this point, we have "grabbed" all the directories
            # We copy data if needed
            self._copyJobDirectories()

            # Set up the environment
            myEnv = self._getEnvironment()
            myEnv['PYTHONIOENCODING'] = 'utf-8'

            # Create files for the error and output streams
            self._outFile = tempfile.TemporaryFile(mode="w+b", suffix="out", prefix="jjob_" + self.name + "_",
                                                   dir="/tmp")
            self._errFile = tempfile.TemporaryFile(mode="w+b", suffix="err", prefix="jjob_" + self.name + "_",
                                                   dir="/tmp")


            # Run the prologue script
            if len(self.jobType.prologue_cmd):
                cmdLine = Template(self.jobType.prologue_cmd)
                cmdLine = cmdLine.substitute(myEnv)
                args = shlex.split(cmdLine)
                self._myLog.debug("%s will run prologue with: %s" % (str(self), str(args)))
                try:
                    subprocess.check_call(args, cwd=myEnv['JJOB_START_HOME'],
                                          env=myEnv, shell=False)
                except subprocess.CalledProcessError:
                    self._myLog.error("Could not run the prologue for %s" % (self))
                    self._startTime = self._endTime = datetime.now()
                    self.signalJobDone(-2, True, "Local job could not run prologue script '%s'" % (str(args)))
                    return self._jobStatus
            # Now go and run the executable
            # Form the command line
            cmdLine = Template(self.jobType.run_cmd + " " + self.run_args)
            cmdLine = cmdLine.substitute(myEnv) # Allow user to use JJOB_* macros
            args = shlex.split(cmdLine)

            self._myLog.debug("%s will execute with: %s" % (str(self), str(args)))

            self._startTime = datetime.now()
            try:
                # Note, we use os.setpgid before running the process so that we can kill the
                # entire process tree (hopefully) if we need to kill the job
                self._process = subprocess.Popen(args, stdout=self._outFile, stderr=self._errFile,
                                                 cwd=myEnv['JJOB_START_HOME'], env=myEnv, shell=False,
                                                 preexec_fn=os.setpgrp)
            except OSError:
                self._myLog.error("Could not run job script for %s" % (self))
                self._startTime = self._endTime = datetime.now()
                self.signalJobDone(-2, True, "Local job could not run '%s'" % (str(args)))
                return self._jobStatus

            return JobObject.RUNNING_LOCAL
        else:
            return self._jobStatus

    def _cleanUp(self):
        """Function to clean up the local job by running the
        epilogue script if needed"""
        if len(self.jobType.epilogue_cmd):
            myEnv = self._getEnvironment()
            cmdLine = Template(self.jobType.epilogue_cmd)
            cmdLine = cmdLine.substitute(myEnv)
            args = shlex.split(cmdLine)
            self._myLog.debug("%s will run epilogue with: %s" % (str(self), str(args)))
            myEnv['PYTHONIOENCODING'] = 'utf-8'
            try:
                subprocess.check_call(args, cwd=myEnv['JJOB_START_HOME'],
                                      env=myEnv, shell=False)
            except subprocess.CalledProcessError:
                self._myLog.error("Could not run the epilogue for %s ... ignoring" % (self))

    def poll(self):
        """Function to check whether a job has finished and if
            so, clean things up properly and update everything
            properly. Returns True if the process has completed (or
            has been terminated) and False if it is still running
        """
        returnCode = self._process.poll()
        if returnCode is None:
            self._myLog.info("Polled %s and it is still running" % (str(self)))
            # Process has not terminated yet. We check for the
            # timeout
            now = datetime.now()
            self._endTime = now
            assert(self.timeout is not None and self.timeout > 0)
            if (now - self._startTime).seconds > self.timeout:
                self._myLog.info("%s timed-out... Killing (PID: %d)" % (str(self), self._process.pid))
                try:
                    os.killpg(self._process.pid, signal.SIGKILL)
                except OSError, e:
                    self._myLog.warning("Killing failed with error: %s" % (e))
                self._process.wait()
                # Run the epilogue script if it exists
                self._cleanUp()
                self.signalJobDone(-1, True, "Local job timed-out")
                self._process = None
                return True
            else:
                self._myLog.debug("%s allowed to continue, ran for %d seconds but has %d seconds" %
                                  (str(self), (now - self._startTime).seconds, self.timeout))
        else:
            # JobObject finished
            self._myLog.info("%s finished running and returned %d" % (str(self), returnCode))
            self._endTime = datetime.now()
            self._cleanUp()
            self.signalJobDone(returnCode, True)
            self._process = None
            return True
        return False


class TorqueJobObject(JobObject):
    """A job that executes using the Torque job scheduler"""

    def __init__(self, inputDict, jobType, dependenceCount):
        super(TorqueJobObject, self).__init__(inputDict, jobType, dependenceCount)
        self._prologueFile = None
        self._epilogueFile = None

    def isLocal(self):
        return False

    def execute(self, jobId):
        """Executes the job using Torque"""
        super(TorqueJobObject, self).execute(jobId)
        if self._recomputeStatus:
            self._updateStatus()
        if self._recomputeDirs:
            self.getDirectories()

        if JobObject.runRemoteJobs == False:
            self._myLog.info("Remote job %s cannot launch because only local jobs are being run" %
                             (str(self)))
            self.signalJobDone(-1, False, "Only local jobs are enabled")
            self._jobStatus = JobObject.DONE_FAIL_LAUNCH
            return self._jobStatus

        if self._jobStatus == JobObject.READY_JOB:

            self._myLog.info("Launching remote job %s of type %s" % (str(self), str(self.jobType)))
            # First we create the environment for the job
            self._createJobDirectories()

            # We now check if the directories are OK to run in
            if not self._grabJobDirectories():
                # We don't actually need to destroy any directories since
                # the only reason we can't grab the directories is
                # we share them (ie: none were created in createJobDirectories)
                return JobObject.READY_JOB
            # At this point, we have "grabbed" all the directories
            # We copy data if needed
            self._copyJobDirectories()

            # Set up the environment
            myEnv = self._getEnvironment()
            myEnv['PYTHONIOENCODING'] = 'utf-8'
            pbsFreeEnv = self._getEnvironment(True)

            # TODO: Maybe move this to the building of the job (but then I don't yet support
            # erroring out of the initializer)
            if len(myEnv['JJOB_SHARED_HOME']) == 0 or \
               len(myEnv['JJOB_PRIVATE_HOME']) <> 0 or \
               myEnv['JJOB_START_HOME'] <> myEnv['JJOB_SHARED_HOME']:
                # Wrong configuration for the job
                self._myLog.info("Remote job's working directories are not configured properly:\n"
                                 "It should have no local home and start in its shared directory. Found values:\n"
                                 "\tSHARED_HOME: %s\n"
                                 "\tPRIVATE_HOME: %s\n"
                                 "\tSTART_HOME: %s\n" % (myEnv['JJOB_SHARED_HOME'], myEnv['JJOB_PRIVATE_HOME'], myEnv['JJOB_START_HOME']))
                self.signalJobDone(-2, True, "Invalid directories for remote job")
                self._jobStatus = JobObject.DONE_FAIL_LAUNCH
                return self._jobStatus
            # All directories are OK for this remote job

            # We need to create the arguments for Qsub
            # Get the string of required resources
            cmdLine = Template(self.jobType.param_cmd + " resources " + self.param_args)
            cmdLine = cmdLine.substitute(myEnv)
            args = shlex.split(cmdLine)
            self._myLog.debug("%s getting resources for Torque job with command '%s'" % (str(self), str(args)))
            resourceString = None

            try:
                resourceString = subprocess.check_output(args, cwd=myEnv['JJOB_START_HOME'], env=myEnv, shell=False)
            except subprocess.CalledProcessError:
                self._myLog.error("Could not get a list of resources for %s... aborting job" % (str(self)))
                self._startTime = self._endTime = datetime.now()
                self.signalJobDone(-2, True, "Remote job could not get resource list")
                return self._jobStatus

            # Now get the queue argument for Qsub
            # Defaults to 'batch'
            cmdLine = Template(self.jobType.param_cmd + " queue " + self.param_args)
            cmdLine = cmdLine.substitute(myEnv)
            args = shlex.split(cmdLine)
            self._myLog.debug("%s getting queue for Torque job with command '%s'" % (str(self), str(args)))
            queueString = "batch"
            try:
                queueString = subprocess.check_output(args, cwd=myEnv['JJOB_START_HOME'], env=myEnv, shell=False)
            except subprocess.CalledProcessError:
                self._myLog.warning("Could not get queue for %s... using 'batch'" % (str(self)))
                queueString = "batch"
            if len(queueString) < 1:
                queueString = "batch"

            # Update the resourceString with the prologue and epilogue commands
            resourceString = resourceString.strip()
            resourceStringNew = re.sub(r"prologue=[^,]*(,|$)", r"",
                                       resourceString).strip(',')
            if len(resourceStringNew) <> len(resourceString):
                self._myLog.warning("Stripping prologue resource spec from %s"
                                    % (self))
            resourceString = resourceStringNew

            resourceStringNew = re.sub(r"epilogue=[^,]*(,|$)", r"",
                                       resourceString).strip(',')
            if len(resourceStringNew) <> len(resourceString):
                self._myLog.warning("Stripping epilogue resource spec from %s"
                                    % (self))
            resourceString = resourceStringNew

            # Prologue and epilogue scripts need to have certain permissions
            # to run properly so we will create a new file with the proper
            # permission
            if len(self.jobType.prologue_cmd):
                cmdLine = Template(self.jobType.prologue_cmd)
                cmdLine = cmdLine.substitute(myEnv)
                self._prologueFile = tempfile.NamedTemporaryFile(mode="w+b", suffix="prologue", prefix="jjob_" + self.name + "_",
                                                                 dir=myEnv['JJOB_SHARED_HOME'], delete=False)
                # Stupid Torque does not pass environment variables to the prologue/epilogue scripts so
                # we do one level substitutions (ie: just in the prologue and epilogue files)
                with open(cmdLine, "r") as tfile:
                    for line in tfile:
                        ttemplate = Template(line)
                        self._prologueFile.write(ttemplate.safe_substitute(pbsFreeEnv))
                self._prologueFile.close()
                os.chmod(self._prologueFile.name, S_IRUSR | S_IWUSR | S_IXUSR)
                resourceString = resourceString + ",prologue=%s" % (self._prologueFile.name)
            if len(self.jobType.epilogue_cmd):
                cmdLine = Template(self.jobType.epilogue_cmd)
                cmdLine = cmdLine.substitute(myEnv)
                self._epilogueFile = tempfile.NamedTemporaryFile(mode="w+b", suffix="epilogue", prefix="jjob_" + self.name + "_",
                                                                 dir=myEnv['JJOB_SHARED_HOME'], delete=False)
                with open(cmdLine, "r") as tfile:
                    for line in tfile:
                        ttemplate = Template(line)
                        self._epilogueFile.write(ttemplate.safe_substitute(pbsFreeEnv))
                self._epilogueFile.close()
                os.chmod(self._epilogueFile.name, S_IRUSR | S_IWUSR | S_IXUSR)
                resourceString = resourceString + ",epilogue=%s" % (self._epilogueFile.name)

            # We deal with the walltime resource requirement because that's
            # how we add the timeout requirement
            resourceString = resourceString.strip()
            resourceStringNew = re.sub(r"walltime=[^,]*(,|$)", r"",
                                       resourceString).strip(',')
            if len(resourceStringNew) <> len(resourceString):
                self._myLog.warning("Stripping walltime resource restrictions from %s... added by timeout option" % (str(self)))

            resourceString = resourceStringNew
            # Set the timeout
            assert(self.timeout is not None and self.timeout > 0)
            resourceString = resourceString + ",walltime=%d" % (self.timeout)

            # Create files for the error and output streams
            self._outFile = tempfile.NamedTemporaryFile(mode="w+b", suffix="out", prefix="jjob_" + self.name + "_",
                                                   dir="/tmp")
            self._errFile = tempfile.NamedTemporaryFile(mode="w+b", suffix="err", prefix="jjob_" + self.name + "_",
                                                   dir="/tmp")

            # Form the argument to qsub
            argCmd = ['/usr/local/bin/qsub', '-d', myEnv['JJOB_START_HOME'],
                      '-e', self._errFile.name, '-j', 'n', '-k', 'n', '-l',
                      resourceString, '-m', 'n', '-N', myEnv['JJOB_NAME'],
                      '-o', self._outFile.name, '-p', '-10', '-q', queueString,
                      '-V']

            # Since qsub only accepts a single script as input, we are going to
            # have to convert to that form
            self._scriptFile = tempfile.NamedTemporaryFile(mode="w+b", suffix="script.sh", prefix="jjob_" + self.name + "_",
                                                           dir="/tmp")

            # Write to the script file
            self._scriptFile.write("#!/bin/bash\n")
            # Form the script line
            scriptLine = Template(self.jobType.run_cmd + " " + self.run_args)
            scriptLine = scriptLine.substitute(myEnv) # Allow user to use JJOB_* macros
            self._myLog.debug("%s command line is: '%s' (writting to '%s')" % (str(self),
                                                                               scriptLine, self._scriptFile.name))
            self._scriptFile.write(scriptLine)
            self._scriptFile.write("\nexit $?\n")
            self._scriptFile.flush()

            argCmd.append(self._scriptFile.name)
            self._myLog.debug("%s will execute remotely with: %s" % (str(self), str(argCmd)))

            self._startTime = datetime.now()
            self._jobNumber = None
            try:
                self._jobNumber = subprocess.check_output(argCmd, cwd=myEnv['JJOB_START_HOME'],
                                                          env=myEnv, shell=False)
            except subprocess.CalledProcessError, err:
                self._myLog.error("Could not launch QSub command for %s... (error '%s' code %d) aborting" %
                                  (str(self), err.output, err.returncode))
                self._myLog.error("Command was %s" % (str(argCmd)))
                self._scriptFile.close() # This will remove this test file
                self._endTime = self._startTime
                self.signalJobDone(-2, True, "QSub returned %d (%s)" % (err.returncode, err.output))
                return self._jobStatus

            # Here we have the job number
            self._jobNumber = self._jobNumber.strip()
            self._myLog.info("%s was added to the Torque job queue with ID '%s'" % (str(self), self._jobNumber))

            return JobObject.RUNNING_REMOTE
        else:
            return self._jobStatus

    def poll(self):
        """Function to check whether a job has finished and if
            so, clean things up properly and update everything
            properly. Returns True if the process has completed (or
            has been terminated) and False if it is still running
        """
        stateCompleteMatch = re.compile("job_state = ([CEHQRTWS])$", re.MULTILINE)
        statusCheckMatch = re.compile("exit_status = ([-0-9]+)$", re.MULTILINE)
        timeCheckMatch = re.compile("resources_used.walltime = ([0-9:]+)$", re.MULTILINE)

        pollArgs = ['/usr/local/bin/qstat', '-f', self._jobNumber]
        self._myLog.debug("%s: Checking for job completion with '%s'" % (str(self), str(pollArgs)))
        pollOutput = None
        try:
            pollOutput = subprocess.check_output(pollArgs, shell=False)
        except subprocess.CalledProcessError:
            self._myLog.debug("%s: Job trace not yet available... will retry" % (str(self)))
            return False

        # Now we can check if we have a completed job
        self._endTime = now = datetime.now()

        matchObj = stateCompleteMatch.search(pollOutput)
        if matchObj is not None and matchObj.group(1) == "C":
            self._myLog.info("Polled %s and it is marked as being complete" % (str(self)))

            matchObj = (statusCheckMatch.search(pollOutput), timeCheckMatch.search(pollOutput))
            if matchObj[0] is not None and matchObj[1] is not None:
                self._myLog.info("%s finished running and returned %s" % (str(self), matchObj[0].group(1)))
                # Compute the endtime based on Torque's information (more precise)
                walltime = matchObj[1].group(1).split(':')
                self._endTime = self._startTime + timedelta(hours=int(walltime[0]), minutes=int(walltime[1]),
                                                            seconds=int(walltime[2]))
                self.signalJobDone(int(matchObj[0].group(1)), True)
            else:
                self._myLog.error("%s completed but cannot read status... marking as aborted" % (str(self)))
                self.signalJobDone(-1, True, "Remote job has unknown job status")

            # In both cases, the job is done and we need to clean up
            self._scriptFile.close()
            return True
        else:
            curStatus = "in an unknown state"
            if matchObj is not None:
                if matchObj.group(1) == "Q":
                    curStatus = "queued"
                elif matchObj.group(1) == "E":
                    curStatus = "exiting"
                elif matchObj.group(1) == "R":
                    curStatus = "running"
                elif matchObj.group(1) == "H":
                    curStatus = "being held"
                else:
                    curStatus = "in a weird state"
            self._myLog.debug("%s is %s, ran for %d seconds" %
                              (str(self), curStatus,
                               (now - self._startTime).seconds))
        return False

    def signalJobDone(self, returnCode, releaseDirs, errString = None):
        super(TorqueJobObject, self).signalJobDone(returnCode, releaseDirs, errString)
        if self._prologueFile is not None:
            os.remove(self._prologueFile.name)
        if self._epilogueFile is not None:
            os.remove(self._epilogueFile.name)

        self._prologueFile = self._epilogueFile = None
